# Option pricing engine

## HOWTO

Project should be opened via dev container: 
- install docker and Dev container extension for VS code. 
- press `f1` and type `reopen in dev container`
- all works!

The dev container brings up three services (see `docker-compose.yml`):
`clickhouse` (analytical store, schema auto-applied from `clickhouse/init/`),
`grafana` (http://localhost:3000, admin/admin) and the `workspace` you code in.

## Data

Raw CME `ivcurve` JSON snapshots live under `data/` (git-ignored). See
`data/README.md` for the folder layout and file format.

## Input format

One file = one snapshot of one option series, JSON wrapped in a root
`"ivcurve"` key:

```json
{
  "ivcurve": {
    "id": "CME_MINI:ESM2026;E2C;20260610",
    "now": "2026-05-07T09:54:21.752997491Z",
    "expirationDate": 20260610,
    "exerciseStyle": "european",
    "interestRate": 0.01,
    "underlyingType": "futures",
    "isPayingDividends": false,
    "marketData": {
      "underlying": { "id": "...", "mode": "...", "bid": {...}|null, "ask": {...}|null, "last": {...}|null },
      "puts":  { "<strike>": { "id": "...", "mode": "...", "bid": ..., "ask": ..., "last": ... } },
      "calls": { "<strike>": { ... } }
    }
  }
}
```

Full field-by-field format, folder layout, and which exchanges use it: see
`data/README.md`.

## Output

Each successfully parsed file writes into four ClickHouse tables (schema:
`clickhouse/init/create_schema.sql`): one row in `underlyings`, one row in
`option_chains`, one row in `underlying_quotes`, and one row per surviving
put/call in `option_quotes`.

## Building the parser

From inside the dev container:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Dependencies (`nlohmann/json`, `clickhouse-cpp`) are fetched automatically by
CMake, so the first configure needs network access.

## Running the importer

The `ope` binary reads commands from an interactive event loop; `insert` imports
a file or a directory (walked recursively) into ClickHouse:

```bash
./build/ope                 # interactive console
> insert data/CME           # import everything
> insert data/CME/CME-6AM2026/5AD_20260529.json   # import one file
> quit

./build/ope insert data/CME # non-interactive one-shot (same command)
```

Files are distributed to a pool of worker threads through a blocking queue; each
worker parses JSON and streams rows into ClickHouse in batches. Connection and
tuning are read from the environment (defaults in parentheses):

| Variable | Purpose |
| --- | --- |
| `CLICKHOUSE_HOST` (`localhost`), `CLICKHOUSE_PORT` (`9000`) | server address (native TCP) |
| `CLICKHOUSE_DB` (`options_pricing`), `CLICKHOUSE_USER` (`default`), `CLICKHOUSE_PASSWORD` (empty) | credentials |
| `OPE_WORKERS` (CPU count) | number of parser/inserter threads |
| `OPE_BATCH_SIZE` (`50000`) | rows accumulated before a batch `INSERT` |

At the end of a run the tool prints a summary:

```text
[insert] done in 24.0s: 30564 ok, 0 failed, 2067514 option rows, 15 option(s) skipped (invalid strike)
```

- **ok / failed** — whole files that parsed successfully vs. were rejected
  outright (missing required field, malformed enum value, bad JSON, wrong
  root shape, etc.). A failed file is skipped entirely; the reason is
  printed as `[insert] skip <file>: <error>`.
- **skipped (invalid strike)** — individual put/call entries dropped from an
  otherwise-valid file because their strike key was malformed/non-finite/
  non-positive (real data has a handful of `"0."` placeholder entries with
  no live quote at all). Each one is logged as
  `[insert] <file>: skipped option <detail>` — never silently discarded.

## Tests

```bash
cmake --build build --target ope_tests -j
./build/ope_tests
```

Unit tests cover `time_util` (timestamp parsing) and `ivcurve_parser`
(required-field/enum/strike validation, skip-not-fail behavior, real-shape
fixtures) — no ClickHouse connection needed, `ope_tests` only links the
parsing code.

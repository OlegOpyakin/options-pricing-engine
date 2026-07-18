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

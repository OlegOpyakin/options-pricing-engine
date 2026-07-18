# `data/` — raw market data (git-ignored)

This directory holds raw option-chain snapshots consumed by the parser. Its
contents are **ignored by git** (see the repo `.gitignore`) because the files are
large and reproducible from the data provider.

## Layout

Test data spans multiple exchanges (CME, CME_MINI, CBOE, CBOT, CBOT_MINI, COMEX,
NASDAQ, NYMEX, NYSE), each delivered as one archive:

```
data/
└── <EXCHANGE>/
    └── <EXCHANGE>-<UNDERLYING>/     # one folder per underlying (futures, stock, ETF, or index)
        └── <PRODUCT>_<YYYYMMDD>.json   # one file per option series (expiration)
```

Examples: `data/CME/CME-6AM2026/5AD_20260529.json` (futures), `data/NASDAQ/NASDAQ-AAL/AAL_20260529.json` (stock).

## File format (`ivcurve` snapshot)

Each JSON file is a single snapshot of one option chain:

```jsonc
{
  "ivcurve": {
    "id": "CME:6AM2026;5AD;20260529",   // market:underlying;product;expiration
    "now": "2026-05-25T14:59:01.800Z",   // snapshot time (same across a batch)
    "expirationDate": 20260529,           // YYYYMMDD as an integer
    "exerciseStyle": "european",          // european | american
    "interestRate": 0.01,
    "underlyingType": "futures",
    "isPayingDividends": false,
    "marketData": {
      "underlying": { "id": "CME:6AM2026", "mode": "realtime",
                      "bid": {"price": .., "size": .., "time": ".."},
                      "ask": {...}, "last": {...} },
      "puts":  { "<strike>": { "id": "..", "mode": "..",
                               "bid": {...}|null, "ask": {...}|null, "last": {...}|null } },
      "calls": { "<strike>": { ... } }
    }
  }
}
```

A `bid`/`ask`/`last` quote is either `null` (no quote) or `{price, size, time}`.
This nullability is preserved in the ClickHouse schema so it can be measured by
the data-quality task.

The parser (implemented separately) reads these files and loads them into the
ClickHouse tables defined in `clickhouse/init/create_schema.sql`.

-- ============================================================================
--  Options Pricing Engine — ClickHouse schema
-- ----------------------------------------------------------------------------
--  Source: CME `ivcurve` JSON snapshots (see data/README.md).
--
--  Design goals (each downstream task is called out where it is supported):
--    * Data-quality assessment      -> Nullable quote fields + MATERIALIZED
--                                      validity flags + v_data_quality
--    * Enumerating option property   -> normalized dimensions + v_option_universe
--      combinations
--    * Pricing model implementation  -> v_pricing_inputs exposes every input a
--                                      model needs (S, K, r, T, style, quotes)
--    * Implied dividend rate         -> v_put_call_pairs aligns the call & put at
--                                      each (underlying, expiration, strike) so
--                                      put-call parity can be solved downstream
--
--  Modelling notes:
--    * The dataset is a point-in-time SNAPSHOT (`now` is identical across a
--      batch), but everything is keyed by `snapshot_time` so repeated snapshots
--      form a natural time series without schema changes.
--    * Grain choices:
--        underlyings        : one row per underlying (slowly-changing dim)
--        option_chains      : one row per (chain, snapshot)
--        underlying_quotes  : one row per (underlying, snapshot)  [fact]
--        option_quotes      : one row per (option, snapshot)      [fact]
--    * Prices are Nullable: a missing quote (`null` in JSON) is semantically
--      different from a 0 price and that difference is what the data-quality
--      task measures.
-- ============================================================================

CREATE DATABASE IF NOT EXISTS options_pricing;

-- ============================================================================
-- 1. DIMENSION: underlyings
--    One row per underlying futures contract (folder = CME-<symbol>).
-- ============================================================================
CREATE TABLE IF NOT EXISTS options_pricing.underlyings
(
    underlying_id    String,                      -- e.g. "CME:6AM2026"
    market           LowCardinality(String),      -- e.g. "CME"
    symbol           String,                      -- e.g. "6AM2026"
    underlying_type  LowCardinality(String)       -- e.g. "futures"
)
ENGINE = ReplacingMergeTree()
ORDER BY underlying_id;

-- ============================================================================
-- 2. DIMENSION: option_chains
--    One row per (underlying, expiration) per snapshot. Holds the contract-level
--    attributes that pricing/parity need (rate, exercise style, expiration).
-- ============================================================================
CREATE TABLE IF NOT EXISTS options_pricing.option_chains
(
    chain_id             String,                  -- "CME:6AM2026;5AD;20260529"
    underlying_id        String,                  -- "CME:6AM2026"
    product_code         LowCardinality(String),  -- "5AD"
    expiration_date      Date,                    -- parsed from expirationDate
    expiration_date_raw  UInt32,                  -- 20260529 (as delivered)
    exercise_style       LowCardinality(String),  -- european | american
    underlying_type      LowCardinality(String),  -- futures
    interest_rate        Float64,                 -- ivcurve.interestRate
    is_paying_dividends  UInt8,                   -- ivcurve.isPayingDividends
    snapshot_time        DateTime64(9, 'UTC'),    -- ivcurve.now
    source_file          String
)
ENGINE = ReplacingMergeTree(snapshot_time)
ORDER BY (chain_id, snapshot_time);

-- ============================================================================
-- 3. FACT: underlying_quotes
--    The underlying (futures) bid/ask/last as captured in each ivcurve file.
--    Grain = one row per (chain, snapshot): every ivcurve file carries its own
--    underlying snapshot, and those captures differ slightly across a
--    underlying's files (same instant, independently sampled). Keeping chain_id
--    lets pricing/parity use the underlying price from the SAME file as the
--    option, instead of an arbitrary one — see the joins in the views below.
-- ============================================================================
CREATE TABLE IF NOT EXISTS options_pricing.underlying_quotes
(
    snapshot_time  DateTime64(9, 'UTC'),
    chain_id       LowCardinality(String),           -- file this capture came from
    underlying_id  LowCardinality(String),
    mode           LowCardinality(String),           -- realtime | snapshot
    quote_time     Nullable(DateTime64(9, 'UTC')),   -- marketData.underlying.time

    bid_price      Nullable(Float64),
    bid_size       Nullable(UInt32),
    bid_time       Nullable(DateTime64(9, 'UTC')),

    ask_price      Nullable(Float64),
    ask_size       Nullable(UInt32),
    ask_time       Nullable(DateTime64(9, 'UTC')),

    last_price     Nullable(Float64),
    last_size      Nullable(UInt32),
    last_time      Nullable(DateTime64(9, 'UTC')),

    -- Derived convenience columns (Nullable propagates when a side is missing).
    mid_price      Nullable(Float64) MATERIALIZED (bid_price + ask_price) / 2,
    spread         Nullable(Float64) MATERIALIZED ask_price - bid_price
)
ENGINE = MergeTree()
PARTITION BY toYYYYMM(snapshot_time)
ORDER BY (underlying_id, chain_id, snapshot_time);

-- ============================================================================
-- 4. FACT: option_quotes  (core analytical table)
--    One row per option (put/call at a strike) per snapshot. A few chain-level
--    attributes are denormalized here (exercise_style, interest_rate,
--    expiration_date) so the common pricing/quality queries need no joins.
--
--    ORDER BY groups every strike of an expiration together and keeps calls &
--    puts adjacent -> cheap self-join for put-call parity and cheap scans for
--    per-chain quality metrics and combination enumeration.
-- ============================================================================
CREATE TABLE IF NOT EXISTS options_pricing.option_quotes
(
    snapshot_time    DateTime64(9, 'UTC'),
    chain_id         LowCardinality(String),
    underlying_id    LowCardinality(String),
    product_code     LowCardinality(String),
    expiration_date  Date,
    strike           Float64,
    option_type      Enum8('put' = 0, 'call' = 1),
    option_id        String,                       -- "CME:5AD260529P0.65"

    -- denormalized chain attributes (pricing inputs, avoid a join on hot path)
    exercise_style   LowCardinality(String),
    interest_rate    Float64,

    mode             LowCardinality(String),       -- realtime | snapshot
    quote_time       Nullable(DateTime64(9, 'UTC')),

    bid_price        Nullable(Float64),
    bid_size         Nullable(UInt32),
    bid_time         Nullable(DateTime64(9, 'UTC')),

    ask_price        Nullable(Float64),
    ask_size         Nullable(UInt32),
    ask_time         Nullable(DateTime64(9, 'UTC')),

    last_price       Nullable(Float64),
    last_size        Nullable(UInt32),
    last_time        Nullable(DateTime64(9, 'UTC')),

    source_file      String,

    -- ---- derived columns (data-quality + pricing convenience) --------------
    mid_price   Nullable(Float64) MATERIALIZED (bid_price + ask_price) / 2,
    spread      Nullable(Float64) MATERIALIZED ask_price - bid_price,
    spread_bps  Nullable(Float64) MATERIALIZED
        if((bid_price + ask_price) / 2 > 0,
           (ask_price - bid_price) / ((bid_price + ask_price) / 2) * 10000,
           NULL),

    -- Quote health flags used by the data-quality task.
    has_bid          UInt8 MATERIALIZED bid_price IS NOT NULL,
    has_ask          UInt8 MATERIALIZED ask_price IS NOT NULL,
    is_two_sided     UInt8 MATERIALIZED bid_price IS NOT NULL AND ask_price IS NOT NULL,
    is_crossed       UInt8 MATERIALIZED bid_price IS NOT NULL AND ask_price IS NOT NULL
                                        AND bid_price > ask_price,
    is_valid_quote   UInt8 MATERIALIZED
        bid_price IS NOT NULL AND ask_price IS NOT NULL
        AND bid_price > 0 AND ask_price > 0
        AND ask_price >= bid_price
        AND bid_size > 0 AND ask_size > 0,
    seconds_since_last_trade Nullable(Int64) MATERIALIZED
        if(last_time IS NULL, NULL, dateDiff('second', last_time, snapshot_time))
)
ENGINE = MergeTree()
PARTITION BY toYYYYMM(expiration_date)
ORDER BY (underlying_id, expiration_date, option_type, strike, snapshot_time);

-- ============================================================================
-- 5. VIEWS
--    Thin, task-oriented projections. They contain NO pricing/greek/parity math
--    (that is the downstream tasks' job) — they only align and expose the
--    inputs those tasks need.
-- ============================================================================

-- 5a. Option universe / property-combination enumeration ---------------------
--     Supports: "Enumerating all combinations of options properties".
CREATE VIEW IF NOT EXISTS options_pricing.v_option_universe AS
SELECT
    underlying_id,
    product_code,
    expiration_date,
    exercise_style,
    option_type,
    strike,
    count()                         AS snapshots_seen,
    max(snapshot_time)              AS last_snapshot
FROM options_pricing.option_quotes
GROUP BY
    underlying_id, product_code, expiration_date,
    exercise_style, option_type, strike;

-- 5b. Pricing inputs ---------------------------------------------------------
--     Supports: "Implementation of the pricing models".
--     Joins each option to its chain and to the underlying quote of the same
--     snapshot, and derives time-to-expiry. No option value is computed here.
CREATE VIEW IF NOT EXISTS options_pricing.v_pricing_inputs AS
SELECT
    oq.snapshot_time                                   AS snapshot_time,
    oq.chain_id                                        AS chain_id,
    oq.underlying_id                                   AS underlying_id,
    oq.option_id                                       AS option_id,
    oq.option_type                                     AS option_type,
    oq.strike                                          AS strike,
    oq.exercise_style                                  AS exercise_style,
    oq.interest_rate                                   AS interest_rate,
    oq.expiration_date                                 AS expiration_date,
    -- time to expiry in years (ACT/365), computed from the snapshot instant
    dateDiff('second', oq.snapshot_time, toDateTime64(oq.expiration_date, 9, 'UTC'))
        / (365.0 * 24 * 3600)                          AS time_to_expiry_years,
    oq.bid_price                                       AS option_bid,
    oq.ask_price                                       AS option_ask,
    oq.mid_price                                       AS option_mid,
    oq.last_price                                      AS option_last,
    uq.bid_price                                       AS underlying_bid,
    uq.ask_price                                       AS underlying_ask,
    uq.mid_price                                       AS underlying_mid,
    uq.last_price                                      AS underlying_last,
    oq.is_valid_quote                                  AS is_valid_quote
FROM options_pricing.option_quotes AS oq
LEFT JOIN options_pricing.underlying_quotes AS uq
       ON oq.chain_id = uq.chain_id
      AND oq.snapshot_time = uq.snapshot_time;

-- 5c. Put-call pairs ---------------------------------------------------------
--     Supports: "Implementing logic for computing the implied dividend rate".
--     Aligns the call and put at the same (underlying, expiration, strike,
--     snapshot) with the underlying price, the rate and time-to-expiry — i.e.
--     every input of put-call parity  C - P = S*e^{-qT} - K*e^{-rT}.
--     The implied dividend q is intentionally NOT solved here.
CREATE VIEW IF NOT EXISTS options_pricing.v_put_call_pairs AS
SELECT
    c.snapshot_time                                    AS snapshot_time,
    c.underlying_id                                    AS underlying_id,
    c.chain_id                                         AS chain_id,
    c.expiration_date                                  AS expiration_date,
    c.strike                                           AS strike,
    c.exercise_style                                   AS exercise_style,
    c.interest_rate                                    AS interest_rate,
    dateDiff('second', c.snapshot_time, toDateTime64(c.expiration_date, 9, 'UTC'))
        / (365.0 * 24 * 3600)                          AS time_to_expiry_years,
    c.mid_price                                        AS call_mid,
    p.mid_price                                        AS put_mid,
    c.bid_price                                        AS call_bid,
    c.ask_price                                        AS call_ask,
    p.bid_price                                        AS put_bid,
    p.ask_price                                        AS put_ask,
    uq.mid_price                                       AS underlying_mid,
    uq.last_price                                      AS underlying_last
FROM options_pricing.option_quotes AS c
INNER JOIN options_pricing.option_quotes AS p
        ON  c.chain_id      = p.chain_id
        AND c.strike        = p.strike
        AND c.snapshot_time = p.snapshot_time
        AND c.option_type   = 'call'
        AND p.option_type   = 'put'
LEFT JOIN options_pricing.underlying_quotes AS uq
        ON  c.chain_id      = uq.chain_id
        AND c.snapshot_time = uq.snapshot_time;

-- 5d. Per-chain data-quality metrics -----------------------------------------
--     Supports: "Assessing input data quality" and the Grafana dashboard.
CREATE VIEW IF NOT EXISTS options_pricing.chain_quality_metrics AS
SELECT
    chain_id,
    underlying_id,
    expiration_date,
    exercise_style,
    snapshot_time                                      AS timestamp,
    count()                                            AS total_quotes,
    countIf(option_type = 'call')                      AS call_count,
    countIf(option_type = 'put')                       AS put_count,
    countIf(is_valid_quote)                            AS valid_quotes_count,
    countIf(NOT is_valid_quote)                        AS invalid_quotes_count,
    countIf(is_two_sided)                              AS two_sided_count,
    countIf(is_crossed)                                AS crossed_count,
    countIf(bid_price IS NULL AND ask_price IS NULL)   AS empty_quote_count,
    round(avgIf(spread_bps, is_valid_quote), 2)        AS avg_spread_bps,
    round(medianIf(spread_bps, is_valid_quote), 2)     AS median_spread_bps,
    min(strike)                                        AS min_strike,
    max(strike)                                        AS max_strike
FROM options_pricing.option_quotes
GROUP BY
    chain_id, underlying_id, expiration_date, exercise_style, snapshot_time;

-- 5e. Snapshot-level data-quality overview -----------------------------------
CREATE VIEW IF NOT EXISTS options_pricing.v_data_quality AS
SELECT
    snapshot_time                                      AS timestamp,
    uniqExact(underlying_id)                           AS underlyings,
    uniqExact(chain_id)                                AS chains,
    count()                                            AS total_quotes,
    countIf(is_valid_quote)                            AS valid_quotes,
    round(100.0 * countIf(is_valid_quote) / count(), 2) AS valid_pct,
    countIf(is_crossed)                                AS crossed_quotes,
    countIf(bid_price IS NULL AND ask_price IS NULL)   AS empty_quotes,
    round(avgIf(spread_bps, is_valid_quote), 2)        AS avg_spread_bps
FROM options_pricing.option_quotes
GROUP BY snapshot_time;

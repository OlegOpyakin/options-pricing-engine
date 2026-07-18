#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ope {

// One side of a quote (bid / ask / last). All fields are optional because the
// source JSON delivers `null` when there is no quote on that side.
struct QuoteSide {
  std::optional<double> price;
  std::optional<uint32_t> size;
  std::optional<int64_t> time_ns;  // nanoseconds since epoch
};

// -> options_pricing.underlyings (dimension)
struct UnderlyingRow {
  std::string underlying_id;   // "CME:6AM2026"
  std::string market;          // "CME"
  std::string symbol;          // "6AM2026"
  std::string underlying_type; // "futures"
};

// -> options_pricing.option_chains (dimension, one per file)
struct ChainRow {
  std::string chain_id;         // "CME:6AM2026;5AD;20260529"
  std::string underlying_id;
  std::string product_code;     // "5AD"
  int32_t expiration_days = 0;  // days since epoch (Date)
  uint32_t expiration_raw = 0;  // 20260529
  std::string exercise_style;   // european | american
  std::string underlying_type;
  double interest_rate = 0.0;
  uint8_t is_paying_dividends = 0;
  int64_t snapshot_ns = 0;
  std::string source_file;
};

// -> options_pricing.underlying_quotes (fact, one per file)
struct UnderlyingQuoteRow {
  int64_t snapshot_ns = 0;
  std::string chain_id;
  std::string underlying_id;
  std::string mode;                    // realtime | snapshot
  std::optional<int64_t> quote_time_ns;
  QuoteSide bid;
  QuoteSide ask;
  QuoteSide last;
};

// -> options_pricing.option_quotes (fact, many per file)
struct OptionRow {
  int64_t snapshot_ns = 0;
  std::string chain_id;
  std::string underlying_id;
  std::string product_code;
  int32_t expiration_days = 0;
  double strike = 0.0;
  int8_t option_type = 0;  // 0 = put, 1 = call (matches Enum8 in schema)
  std::string option_id;
  std::string exercise_style;
  double interest_rate = 0.0;
  std::string mode;
  std::optional<int64_t> quote_time_ns;
  QuoteSide bid;
  QuoteSide ask;
  QuoteSide last;
  std::string source_file;
};

// The full parsed content of a single ivcurve JSON file.
struct ParsedFile {
  UnderlyingRow underlying;
  ChainRow chain;
  UnderlyingQuoteRow underlying_quote;
  std::vector<OptionRow> options;
};

}  // namespace ope

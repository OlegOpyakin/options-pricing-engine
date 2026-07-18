#include "ope/ivcurve_parser.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "ope/time_util.hpp"

namespace ope {

namespace {

using nlohmann::json;

// Split "CME:6AM2026;5AD;20260529" on ';'.
std::vector<std::string> split(const std::string& s, char delim) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == delim) {
      out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  out.push_back(cur);
  return out;
}

// A required field was missing, malformed, or held an unsupported value.
// Always fatal for the whole file — see parser_specification.md §9.1.
[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

void require_enum(const std::string& value, std::initializer_list<const char*> allowed,
                  const char* field_name) {
  for (const char* candidate : allowed) {
    if (value == candidate) return;
  }
  fail(std::string(field_name) + ": unsupported value '" + value + "'");
}

// Strict strike-key parsing (parser_specification.md §7 / §24.2): the whole
// key must be consumed, the result must be finite, and a strike is a price so
// it must be strictly positive. std::stod alone enforces none of these — it
// accepts trailing garbage and "nan"/"inf" text silently.
double parse_strike(const std::string& key) {
  if (key.empty()) fail("strike key is empty");
  char* end = nullptr;
  const double value = std::strtod(key.c_str(), &end);
  if (end != key.c_str() + key.size()) {
    fail("strike key not fully consumed: '" + key + "'");
  }
  if (!std::isfinite(value)) {
    fail("strike key is not finite: '" + key + "'");
  }
  if (value <= 0.0) {
    fail("strike key must be > 0: '" + key + "'");
  }
  return value;
}

// Required top-level/marketData timestamp: malformed input is fatal, it must
// never silently collapse to the epoch sentinel (§14.3, §24.3).
int64_t require_time(const json& obj, const char* key) {
  const std::string raw = obj.at(key).get<std::string>();
  const auto parsed = timeutil::parse_iso8601_ns(raw);
  if (!parsed) fail(std::string(key) + ": malformed timestamp '" + raw + "'");
  return *parsed;
}

// Read one bid/ask/last node, which is either JSON null or {price,size,time}.
QuoteSide parse_side(const json& node) {
  QuoteSide side;
  if (node.is_null()) return side;
  if (auto it = node.find("price"); it != node.end() && !it->is_null())
    side.price = it->get<double>();
  if (auto it = node.find("size"); it != node.end() && !it->is_null())
    side.size = it->get<uint32_t>();
  if (auto it = node.find("time"); it != node.end() && !it->is_null()) {
    const std::string raw = it->get<std::string>();
    const auto parsed = timeutil::parse_iso8601_ns(raw);
    if (!parsed) fail(std::string("quote time: malformed timestamp '") + raw + "'");
    side.time_ns = *parsed;
  }
  return side;
}

// Per-instrument "time" (present but null is legitimate; present-and-malformed
// is fatal per §9.1 — a format error, not a missing value).
std::optional<int64_t> parse_optional_time(const json& obj, const char* key) {
  auto it = obj.find(key);
  if (it == obj.end() || it->is_null()) return std::nullopt;
  const std::string raw = it->get<std::string>();
  const auto parsed = timeutil::parse_iso8601_ns(raw);
  if (!parsed) fail(std::string(key) + ": malformed timestamp '" + raw + "'");
  return parsed;
}

// Parse the puts/calls map into OptionRows sharing the given chain context.
// A malformed strike key skips just that one entry (recorded in `skipped`)
// rather than failing the whole file — see ParsedFile::skipped_options.
void parse_option_map(const json& node, int8_t option_type, const ChainRow& chain,
                      const std::string& source_file, std::vector<OptionRow>& out,
                      std::vector<std::string>& skipped) {
  if (!node.is_object()) fail("puts/calls collection is not an object");
  const char* type_name = option_type == 1 ? "call" : "put";
  for (auto it = node.begin(); it != node.end(); ++it) {
    const json& entry = it.value();

    double strike = 0.0;
    try {
      strike = parse_strike(it.key());  // map key is the strike, e.g. "0.65"
    } catch (const std::exception& e) {
      // entry.value() throws if entry isn't a JSON object at all — a doubly
      // malformed record (bad strike key AND a non-object value) must still
      // only skip itself, not the whole file, so guard instead of calling
      // .value() unconditionally.
      const std::string option_id =
          entry.is_object() ? entry.value("id", std::string{"<no id>"}) : std::string{"<malformed entry>"};
      skipped.push_back(std::string(type_name) + " '" + it.key() + "' (" + option_id +
                         "): " + e.what());
      continue;
    }

    OptionRow row;
    row.snapshot_ns = chain.snapshot_ns;
    row.chain_id = chain.chain_id;
    row.underlying_id = chain.underlying_id;
    row.product_code = chain.product_code;
    row.expiration_days = chain.expiration_days;
    row.strike = strike;
    row.option_type = option_type;
    row.option_id = entry.at("id").get<std::string>();
    row.exercise_style = chain.exercise_style;
    row.interest_rate = chain.interest_rate;
    row.mode = entry.at("mode").get<std::string>();
    require_enum(row.mode, {"realtime", "snapshot"}, "mode");
    row.quote_time_ns = parse_optional_time(entry, "time");
    if (auto f = entry.find("bid"); f != entry.end()) row.bid = parse_side(*f);
    if (auto f = entry.find("ask"); f != entry.end()) row.ask = parse_side(*f);
    if (auto f = entry.find("last"); f != entry.end()) row.last = parse_side(*f);
    row.source_file = source_file;
    out.push_back(std::move(row));
  }
}

}  // namespace

std::optional<ParsedFile> parse_ivcurve_file(const std::string& path,
                                             std::string& error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = "cannot open file";
    return std::nullopt;
  }

  json root;
  try {
    in >> root;
  } catch (const std::exception& e) {
    error = std::string("json parse error: ") + e.what();
    return std::nullopt;
  }

  const auto iv_it = root.find("ivcurve");
  if (iv_it == root.end() || !iv_it->is_object()) {
    error = "missing 'ivcurve' object";
    return std::nullopt;
  }
  const json& iv = *iv_it;

  try {
    ParsedFile pf;

    const std::string chain_id = iv.at("id").get<std::string>();
    const int64_t snapshot_ns = require_time(iv, "now");
    const uint32_t expiration_raw = iv.at("expirationDate").get<uint32_t>();
    const std::string exercise_style = iv.at("exerciseStyle").get<std::string>();
    require_enum(exercise_style, {"american", "european"}, "exerciseStyle");
    const std::string underlying_type = iv.at("underlyingType").get<std::string>();
    // "fund" (not "etf") is how this data source labels ETFs; "dr" is a
    // depositary receipt. Measured directly across the full real dataset —
    // see parser_specification.md §25.
    require_enum(underlying_type, {"stock", "futures", "index", "fund", "dr"},
                "underlyingType");
    const double interest_rate = iv.at("interestRate").get<double>();
    const bool is_paying_dividends = iv.at("isPayingDividends").get<bool>();

    // chain_id = "<underlying_id>;<product_code>;<expiration>"
    const auto parts = split(chain_id, ';');
    const std::string underlying_id = parts.size() > 0 ? parts[0] : std::string{};
    const std::string product_code = parts.size() > 1 ? parts[1] : std::string{};

    // underlying_id = "<market>:<symbol>"
    std::string market, symbol;
    if (const auto colon = underlying_id.find(':'); colon != std::string::npos) {
      market = underlying_id.substr(0, colon);
      symbol = underlying_id.substr(colon + 1);
    } else {
      symbol = underlying_id;
    }

    // --- underlying dimension ---
    pf.underlying.underlying_id = underlying_id;
    pf.underlying.market = market;
    pf.underlying.symbol = symbol;
    pf.underlying.underlying_type = underlying_type;

    // --- chain dimension ---
    pf.chain.chain_id = chain_id;
    pf.chain.underlying_id = underlying_id;
    pf.chain.product_code = product_code;
    pf.chain.expiration_days = timeutil::yyyymmdd_to_days(expiration_raw);
    pf.chain.expiration_raw = expiration_raw;
    pf.chain.exercise_style = exercise_style;
    pf.chain.underlying_type = underlying_type;
    pf.chain.interest_rate = interest_rate;
    pf.chain.is_paying_dividends = is_paying_dividends ? 1 : 0;
    pf.chain.snapshot_ns = snapshot_ns;
    pf.chain.source_file = path;

    const json& md = iv.at("marketData");
    pf.market_data_id = md.at("id").get<std::string>();
    pf.market_data_time_ns = require_time(md, "time");

    // --- underlying quote fact ---
    const json& u = md.at("underlying");
    const std::string underlying_json_id = u.at("id").get<std::string>();
    if (underlying_json_id != underlying_id) {
      fail("marketData.underlying.id ('" + underlying_json_id +
           "') does not match the underlying id derived from ivcurve.id ('" + underlying_id +
           "')");
    }
    pf.underlying_quote.snapshot_ns = snapshot_ns;
    pf.underlying_quote.chain_id = chain_id;
    pf.underlying_quote.underlying_id = underlying_id;
    pf.underlying_quote.mode = u.at("mode").get<std::string>();
    require_enum(pf.underlying_quote.mode, {"realtime", "snapshot"}, "mode");
    pf.underlying_quote.quote_time_ns = parse_optional_time(u, "time");
    if (auto f = u.find("bid"); f != u.end()) pf.underlying_quote.bid = parse_side(*f);
    if (auto f = u.find("ask"); f != u.end()) pf.underlying_quote.ask = parse_side(*f);
    if (auto f = u.find("last"); f != u.end()) pf.underlying_quote.last = parse_side(*f);

    // --- option quote facts ---
    parse_option_map(md.at("puts"), /*put*/ 0, pf.chain, path, pf.options, pf.skipped_options);
    parse_option_map(md.at("calls"), /*call*/ 1, pf.chain, path, pf.options, pf.skipped_options);

    return pf;
  } catch (const std::exception& e) {
    error = std::string("schema error: ") + e.what();
    return std::nullopt;
  }
}

}  // namespace ope

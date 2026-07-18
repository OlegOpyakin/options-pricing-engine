#include "ope/ivcurve_parser.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
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

// Read one bid/ask/last node, which is either JSON null or {price,size,time}.
QuoteSide parse_side(const json& node) {
  QuoteSide side;
  if (node.is_null()) return side;
  if (auto it = node.find("price"); it != node.end() && !it->is_null())
    side.price = it->get<double>();
  if (auto it = node.find("size"); it != node.end() && !it->is_null())
    side.size = it->get<uint32_t>();
  if (auto it = node.find("time"); it != node.end() && !it->is_null())
    side.time_ns = timeutil::parse_iso8601_ns(it->get<std::string>());
  return side;
}

std::optional<int64_t> parse_optional_time(const json& obj, const char* key) {
  if (auto it = obj.find(key); it != obj.end() && !it->is_null())
    return timeutil::parse_iso8601_ns(it->get<std::string>());
  return std::nullopt;
}

// Parse the puts/calls map into OptionRows sharing the given chain context.
void parse_option_map(const json& node, int8_t option_type, const ChainRow& chain,
                      const std::string& source_file, std::vector<OptionRow>& out) {
  if (!node.is_object()) return;
  for (auto it = node.begin(); it != node.end(); ++it) {
    const json& entry = it.value();
    OptionRow row;
    row.snapshot_ns = chain.snapshot_ns;
    row.chain_id = chain.chain_id;
    row.underlying_id = chain.underlying_id;
    row.product_code = chain.product_code;
    row.expiration_days = chain.expiration_days;
    row.strike = std::stod(it.key());  // map key is the strike, e.g. "0.65"
    row.option_type = option_type;
    if (auto f = entry.find("id"); f != entry.end() && !f->is_null())
      row.option_id = f->get<std::string>();
    row.exercise_style = chain.exercise_style;
    row.interest_rate = chain.interest_rate;
    if (auto f = entry.find("mode"); f != entry.end() && !f->is_null())
      row.mode = f->get<std::string>();
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
    const int64_t snapshot_ns = timeutil::parse_iso8601_ns(iv.at("now").get<std::string>());
    const uint32_t expiration_raw = iv.at("expirationDate").get<uint32_t>();
    const std::string exercise_style = iv.value("exerciseStyle", std::string{});
    const std::string underlying_type = iv.value("underlyingType", std::string{});
    const double interest_rate = iv.value("interestRate", 0.0);
    const bool is_paying_dividends = iv.value("isPayingDividends", false);

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

    // --- underlying quote fact ---
    const json& u = md.at("underlying");
    pf.underlying_quote.snapshot_ns = snapshot_ns;
    pf.underlying_quote.chain_id = chain_id;
    pf.underlying_quote.underlying_id = underlying_id;
    pf.underlying_quote.mode = u.value("mode", std::string{});
    pf.underlying_quote.quote_time_ns = parse_optional_time(u, "time");
    if (auto f = u.find("bid"); f != u.end()) pf.underlying_quote.bid = parse_side(*f);
    if (auto f = u.find("ask"); f != u.end()) pf.underlying_quote.ask = parse_side(*f);
    if (auto f = u.find("last"); f != u.end()) pf.underlying_quote.last = parse_side(*f);

    // --- option quote facts ---
    if (auto f = md.find("puts"); f != md.end())
      parse_option_map(*f, /*put*/ 0, pf.chain, path, pf.options);
    if (auto f = md.find("calls"); f != md.end())
      parse_option_map(*f, /*call*/ 1, pf.chain, path, pf.options);

    return pf;
  } catch (const std::exception& e) {
    error = std::string("schema error: ") + e.what();
    return std::nullopt;
  }
}

}  // namespace ope

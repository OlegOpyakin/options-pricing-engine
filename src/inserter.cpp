#include "ope/inserter.hpp"

#include <clickhouse/client.h>
#include <clickhouse/columns/date.h>
#include <clickhouse/columns/enum.h>
#include <clickhouse/columns/lowcardinality.h>
#include <clickhouse/columns/nullable.h>
#include <clickhouse/columns/numeric.h>
#include <clickhouse/columns/string.h>
#include <clickhouse/types/types.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "ope/blocking_queue.hpp"
#include "ope/ivcurve_parser.hpp"

namespace ope {
namespace {

using namespace clickhouse;
namespace fs = std::filesystem;

using LCString = ColumnLowCardinalityT<ColumnString>;

constexpr int64_t kSecondsPerDay = 86400;

// --- Nullable column builders ----------------------------------------------
// clickhouse-cpp represents Nullable(T) as a nested value column + a UInt8 null
// mask. These helpers keep the two in lock-step and hand back a wrapped column
// at flush time.
struct NullableFloat64 {
  std::shared_ptr<ColumnFloat64> data = std::make_shared<ColumnFloat64>();
  std::shared_ptr<ColumnUInt8> nulls = std::make_shared<ColumnUInt8>();
  void append(const std::optional<double>& v) {
    data->Append(v.value_or(0.0));
    nulls->Append(v.has_value() ? 0 : 1);
  }
  ColumnRef column() { return std::make_shared<ColumnNullable>(data, nulls); }
  void reset() {
    data = std::make_shared<ColumnFloat64>();
    nulls = std::make_shared<ColumnUInt8>();
  }
};

struct NullableUInt32 {
  std::shared_ptr<ColumnUInt32> data = std::make_shared<ColumnUInt32>();
  std::shared_ptr<ColumnUInt8> nulls = std::make_shared<ColumnUInt8>();
  void append(const std::optional<uint32_t>& v) {
    data->Append(v.value_or(0));
    nulls->Append(v.has_value() ? 0 : 1);
  }
  ColumnRef column() { return std::make_shared<ColumnNullable>(data, nulls); }
  void reset() {
    data = std::make_shared<ColumnUInt32>();
    nulls = std::make_shared<ColumnUInt8>();
  }
};

struct NullableDateTime64 {
  std::shared_ptr<ColumnDateTime64> data = std::make_shared<ColumnDateTime64>(9);
  std::shared_ptr<ColumnUInt8> nulls = std::make_shared<ColumnUInt8>();
  void append(const std::optional<int64_t>& v) {
    data->Append(v.value_or(0));
    nulls->Append(v.has_value() ? 0 : 1);
  }
  ColumnRef column() { return std::make_shared<ColumnNullable>(data, nulls); }
  void reset() {
    data = std::make_shared<ColumnDateTime64>(9);
    nulls = std::make_shared<ColumnUInt8>();
  }
};

// --- Per-table batch builders ----------------------------------------------

struct UnderlyingsBatch {
  std::shared_ptr<ColumnString> underlying_id = std::make_shared<ColumnString>();
  std::shared_ptr<LCString> market = std::make_shared<LCString>();
  std::shared_ptr<ColumnString> symbol = std::make_shared<ColumnString>();
  std::shared_ptr<LCString> underlying_type = std::make_shared<LCString>();
  std::size_t count = 0;

  void append(const UnderlyingRow& r) {
    underlying_id->Append(r.underlying_id);
    market->Append(r.market);
    symbol->Append(r.symbol);
    underlying_type->Append(r.underlying_type);
    ++count;
  }

  void flush(Client& client) {
    if (count == 0) return;
    Block block;
    block.AppendColumn("underlying_id", underlying_id);
    block.AppendColumn("market", market);
    block.AppendColumn("symbol", symbol);
    block.AppendColumn("underlying_type", underlying_type);
    client.Insert("options_pricing.underlyings", block);
    underlying_id = std::make_shared<ColumnString>();
    market = std::make_shared<LCString>();
    symbol = std::make_shared<ColumnString>();
    underlying_type = std::make_shared<LCString>();
    count = 0;
  }
};

struct ChainsBatch {
  std::shared_ptr<ColumnString> chain_id = std::make_shared<ColumnString>();
  std::shared_ptr<ColumnString> underlying_id = std::make_shared<ColumnString>();
  std::shared_ptr<LCString> product_code = std::make_shared<LCString>();
  std::shared_ptr<ColumnDate> expiration_date = std::make_shared<ColumnDate>();
  std::shared_ptr<ColumnUInt32> expiration_date_raw = std::make_shared<ColumnUInt32>();
  std::shared_ptr<LCString> exercise_style = std::make_shared<LCString>();
  std::shared_ptr<LCString> underlying_type = std::make_shared<LCString>();
  std::shared_ptr<ColumnFloat64> interest_rate = std::make_shared<ColumnFloat64>();
  std::shared_ptr<ColumnUInt8> is_paying_dividends = std::make_shared<ColumnUInt8>();
  std::shared_ptr<ColumnDateTime64> snapshot_time = std::make_shared<ColumnDateTime64>(9);
  std::shared_ptr<ColumnString> source_file = std::make_shared<ColumnString>();
  std::size_t count = 0;

  void append(const ChainRow& r) {
    chain_id->Append(r.chain_id);
    underlying_id->Append(r.underlying_id);
    product_code->Append(r.product_code);
    expiration_date->Append(static_cast<std::time_t>(r.expiration_days) * kSecondsPerDay);
    expiration_date_raw->Append(r.expiration_raw);
    exercise_style->Append(r.exercise_style);
    underlying_type->Append(r.underlying_type);
    interest_rate->Append(r.interest_rate);
    is_paying_dividends->Append(r.is_paying_dividends);
    snapshot_time->Append(r.snapshot_ns);
    source_file->Append(r.source_file);
    ++count;
  }

  void flush(Client& client) {
    if (count == 0) return;
    Block block;
    block.AppendColumn("chain_id", chain_id);
    block.AppendColumn("underlying_id", underlying_id);
    block.AppendColumn("product_code", product_code);
    block.AppendColumn("expiration_date", expiration_date);
    block.AppendColumn("expiration_date_raw", expiration_date_raw);
    block.AppendColumn("exercise_style", exercise_style);
    block.AppendColumn("underlying_type", underlying_type);
    block.AppendColumn("interest_rate", interest_rate);
    block.AppendColumn("is_paying_dividends", is_paying_dividends);
    block.AppendColumn("snapshot_time", snapshot_time);
    block.AppendColumn("source_file", source_file);
    client.Insert("options_pricing.option_chains", block);
    chain_id = std::make_shared<ColumnString>();
    underlying_id = std::make_shared<ColumnString>();
    product_code = std::make_shared<LCString>();
    expiration_date = std::make_shared<ColumnDate>();
    expiration_date_raw = std::make_shared<ColumnUInt32>();
    exercise_style = std::make_shared<LCString>();
    underlying_type = std::make_shared<LCString>();
    interest_rate = std::make_shared<ColumnFloat64>();
    is_paying_dividends = std::make_shared<ColumnUInt8>();
    snapshot_time = std::make_shared<ColumnDateTime64>(9);
    source_file = std::make_shared<ColumnString>();
    count = 0;
  }
};

struct UnderlyingQuotesBatch {
  std::shared_ptr<ColumnDateTime64> snapshot_time = std::make_shared<ColumnDateTime64>(9);
  std::shared_ptr<LCString> chain_id = std::make_shared<LCString>();
  std::shared_ptr<LCString> underlying_id = std::make_shared<LCString>();
  std::shared_ptr<LCString> mode = std::make_shared<LCString>();
  NullableDateTime64 quote_time;
  NullableFloat64 bid_price;
  NullableUInt32 bid_size;
  NullableDateTime64 bid_time;
  NullableFloat64 ask_price;
  NullableUInt32 ask_size;
  NullableDateTime64 ask_time;
  NullableFloat64 last_price;
  NullableUInt32 last_size;
  NullableDateTime64 last_time;
  std::size_t count = 0;

  void append(const UnderlyingQuoteRow& r) {
    snapshot_time->Append(r.snapshot_ns);
    chain_id->Append(r.chain_id);
    underlying_id->Append(r.underlying_id);
    mode->Append(r.mode);
    quote_time.append(r.quote_time_ns);
    bid_price.append(r.bid.price);
    bid_size.append(r.bid.size);
    bid_time.append(r.bid.time_ns);
    ask_price.append(r.ask.price);
    ask_size.append(r.ask.size);
    ask_time.append(r.ask.time_ns);
    last_price.append(r.last.price);
    last_size.append(r.last.size);
    last_time.append(r.last.time_ns);
    ++count;
  }

  void flush(Client& client) {
    if (count == 0) return;
    Block block;
    block.AppendColumn("snapshot_time", snapshot_time);
    block.AppendColumn("chain_id", chain_id);
    block.AppendColumn("underlying_id", underlying_id);
    block.AppendColumn("mode", mode);
    block.AppendColumn("quote_time", quote_time.column());
    block.AppendColumn("bid_price", bid_price.column());
    block.AppendColumn("bid_size", bid_size.column());
    block.AppendColumn("bid_time", bid_time.column());
    block.AppendColumn("ask_price", ask_price.column());
    block.AppendColumn("ask_size", ask_size.column());
    block.AppendColumn("ask_time", ask_time.column());
    block.AppendColumn("last_price", last_price.column());
    block.AppendColumn("last_size", last_size.column());
    block.AppendColumn("last_time", last_time.column());
    client.Insert("options_pricing.underlying_quotes", block);

    snapshot_time = std::make_shared<ColumnDateTime64>(9);
    chain_id = std::make_shared<LCString>();
    underlying_id = std::make_shared<LCString>();
    mode = std::make_shared<LCString>();
    quote_time.reset();
    bid_price.reset(); bid_size.reset(); bid_time.reset();
    ask_price.reset(); ask_size.reset(); ask_time.reset();
    last_price.reset(); last_size.reset(); last_time.reset();
    count = 0;
  }
};

struct OptionQuotesBatch {
  TypeRef option_type_enum = Type::CreateEnum8({{"put", 0}, {"call", 1}});

  std::shared_ptr<ColumnDateTime64> snapshot_time = std::make_shared<ColumnDateTime64>(9);
  std::shared_ptr<LCString> chain_id = std::make_shared<LCString>();
  std::shared_ptr<LCString> underlying_id = std::make_shared<LCString>();
  std::shared_ptr<LCString> product_code = std::make_shared<LCString>();
  std::shared_ptr<ColumnDate> expiration_date = std::make_shared<ColumnDate>();
  std::shared_ptr<ColumnFloat64> strike = std::make_shared<ColumnFloat64>();
  std::shared_ptr<ColumnEnum8> option_type = std::make_shared<ColumnEnum8>(option_type_enum);
  std::shared_ptr<ColumnString> option_id = std::make_shared<ColumnString>();
  std::shared_ptr<LCString> exercise_style = std::make_shared<LCString>();
  std::shared_ptr<ColumnFloat64> interest_rate = std::make_shared<ColumnFloat64>();
  std::shared_ptr<LCString> mode = std::make_shared<LCString>();
  NullableDateTime64 quote_time;
  NullableFloat64 bid_price;
  NullableUInt32 bid_size;
  NullableDateTime64 bid_time;
  NullableFloat64 ask_price;
  NullableUInt32 ask_size;
  NullableDateTime64 ask_time;
  NullableFloat64 last_price;
  NullableUInt32 last_size;
  NullableDateTime64 last_time;
  std::shared_ptr<ColumnString> source_file = std::make_shared<ColumnString>();
  std::size_t count = 0;

  void append(const OptionRow& r) {
    snapshot_time->Append(r.snapshot_ns);
    chain_id->Append(r.chain_id);
    underlying_id->Append(r.underlying_id);
    product_code->Append(r.product_code);
    expiration_date->Append(static_cast<std::time_t>(r.expiration_days) * kSecondsPerDay);
    strike->Append(r.strike);
    option_type->Append(r.option_type);
    option_id->Append(r.option_id);
    exercise_style->Append(r.exercise_style);
    interest_rate->Append(r.interest_rate);
    mode->Append(r.mode);
    quote_time.append(r.quote_time_ns);
    bid_price.append(r.bid.price);
    bid_size.append(r.bid.size);
    bid_time.append(r.bid.time_ns);
    ask_price.append(r.ask.price);
    ask_size.append(r.ask.size);
    ask_time.append(r.ask.time_ns);
    last_price.append(r.last.price);
    last_size.append(r.last.size);
    last_time.append(r.last.time_ns);
    source_file->Append(r.source_file);
    ++count;
  }

  void flush(Client& client) {
    if (count == 0) return;
    Block block;
    block.AppendColumn("snapshot_time", snapshot_time);
    block.AppendColumn("chain_id", chain_id);
    block.AppendColumn("underlying_id", underlying_id);
    block.AppendColumn("product_code", product_code);
    block.AppendColumn("expiration_date", expiration_date);
    block.AppendColumn("strike", strike);
    block.AppendColumn("option_type", option_type);
    block.AppendColumn("option_id", option_id);
    block.AppendColumn("exercise_style", exercise_style);
    block.AppendColumn("interest_rate", interest_rate);
    block.AppendColumn("mode", mode);
    block.AppendColumn("quote_time", quote_time.column());
    block.AppendColumn("bid_price", bid_price.column());
    block.AppendColumn("bid_size", bid_size.column());
    block.AppendColumn("bid_time", bid_time.column());
    block.AppendColumn("ask_price", ask_price.column());
    block.AppendColumn("ask_size", ask_size.column());
    block.AppendColumn("ask_time", ask_time.column());
    block.AppendColumn("last_price", last_price.column());
    block.AppendColumn("last_size", last_size.column());
    block.AppendColumn("last_time", last_time.column());
    block.AppendColumn("source_file", source_file);
    client.Insert("options_pricing.option_quotes", block);

    snapshot_time = std::make_shared<ColumnDateTime64>(9);
    chain_id = std::make_shared<LCString>();
    underlying_id = std::make_shared<LCString>();
    product_code = std::make_shared<LCString>();
    expiration_date = std::make_shared<ColumnDate>();
    strike = std::make_shared<ColumnFloat64>();
    option_type = std::make_shared<ColumnEnum8>(option_type_enum);
    option_id = std::make_shared<ColumnString>();
    exercise_style = std::make_shared<LCString>();
    interest_rate = std::make_shared<ColumnFloat64>();
    mode = std::make_shared<LCString>();
    quote_time.reset();
    bid_price.reset(); bid_size.reset(); bid_time.reset();
    ask_price.reset(); ask_size.reset(); ask_time.reset();
    last_price.reset(); last_size.reset(); last_time.reset();
    source_file = std::make_shared<ColumnString>();
    count = 0;
  }
};

// All four table buffers owned by a single worker.
struct WorkerBatches {
  UnderlyingsBatch underlyings;
  ChainsBatch chains;
  UnderlyingQuotesBatch underlying_quotes;
  OptionQuotesBatch option_quotes;

  void flush_full(Client& client, std::size_t batch_size) {
    if (option_quotes.count >= batch_size) option_quotes.flush(client);
    if (underlyings.count >= batch_size) underlyings.flush(client);
    if (chains.count >= batch_size) chains.flush(client);
    if (underlying_quotes.count >= batch_size) underlying_quotes.flush(client);
  }

  void flush_all(Client& client) {
    option_quotes.flush(client);
    underlyings.flush(client);
    chains.flush(client);
    underlying_quotes.flush(client);
  }
};

ClientOptions make_options(const ChConfig& cfg) {
  return ClientOptions()
      .SetHost(cfg.host)
      .SetPort(cfg.port)
      .SetUser(cfg.user)
      .SetPassword(cfg.password)
      .SetDefaultDatabase(cfg.database);
}

// Case-insensitive ".json" check — a stray ".JSON" export should still be
// picked up, not silently skipped with no diagnostic.
bool has_json_extension(const fs::path& p) {
  std::string ext = p.extension().string();
  for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return ext == ".json";
}

// Collect the *.json files to import. A single file is taken as-is; a directory
// is walked recursively.
std::vector<std::string> collect_files(const std::string& path, std::string& error) {
  std::vector<std::string> files;
  std::error_code ec;
  const fs::path p(path);

  const auto status = fs::status(p, ec);
  if (ec) {
    error = "cannot stat path: " + ec.message();
    return files;
  }

  if (fs::is_regular_file(status)) {
    files.push_back(p.string());
    return files;
  }
  if (fs::is_directory(status)) {
    for (auto it = fs::recursive_directory_iterator(p, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (it->is_regular_file() && has_json_extension(it->path())) {
        files.push_back(it->path().string());
      }
    }
    if (ec) error = "error while walking directory: " + ec.message();
    return files;
  }

  error = "path is neither a file nor a directory";
  return files;
}

}  // namespace

Inserter::Inserter(ChConfig config) : config_(std::move(config)) {}

bool Inserter::insert(const std::string& path) {
  using clock = std::chrono::steady_clock;
  const auto started = clock::now();

  std::string error;
  std::vector<std::string> files = collect_files(path, error);
  if (!error.empty()) {
    std::cerr << "[insert] " << error << "\n";
    return false;
  }
  if (files.empty()) {
    std::cerr << "[insert] no .json files found at '" << path << "'\n";
    return false;
  }

  // Fail fast if ClickHouse is unreachable, with a clear message.
  try {
    Client probe(make_options(config_));
    probe.Execute("SELECT 1");
  } catch (const std::exception& e) {
    std::cerr << "[insert] cannot connect to ClickHouse at " << config_.host << ":"
              << config_.port << " (" << e.what() << ")\n";
    return false;
  }

  const std::size_t worker_count = std::min(config_.workers, files.size());
  std::cout << "[insert] " << files.size() << " file(s), " << worker_count
            << " worker(s), batch=" << config_.batch_size << "\n";

  BlockingQueue<std::string> queue;
  for (auto& f : files) queue.push(std::move(f));
  queue.close();

  std::atomic<std::size_t> files_ok{0};
  std::atomic<std::size_t> files_failed{0};
  std::atomic<std::size_t> option_rows{0};
  std::atomic<std::size_t> options_skipped{0};
  std::atomic<bool> fatal{false};
  std::mutex log_mutex;

  auto worker = [&]() {
    Client client(make_options(config_));
    WorkerBatches batches;
    try {
      while (auto file = queue.pop()) {
        std::string perr;
        auto parsed = parse_ivcurve_file(*file, perr);
        if (!parsed) {
          files_failed.fetch_add(1);
          std::lock_guard<std::mutex> lk(log_mutex);
          std::cerr << "[insert] skip " << *file << ": " << perr << "\n";
          continue;
        }
        if (!parsed->skipped_options.empty()) {
          options_skipped.fetch_add(parsed->skipped_options.size());
          std::lock_guard<std::mutex> lk(log_mutex);
          for (const auto& diag : parsed->skipped_options) {
            std::cerr << "[insert] " << *file << ": skipped option " << diag << "\n";
          }
        }
        batches.underlyings.append(parsed->underlying);
        batches.chains.append(parsed->chain);
        batches.underlying_quotes.append(parsed->underlying_quote);
        for (const auto& o : parsed->options) batches.option_quotes.append(o);
        option_rows.fetch_add(parsed->options.size());
        files_ok.fetch_add(1);
        batches.flush_full(client, config_.batch_size);
      }
      batches.flush_all(client);
    } catch (const std::exception& e) {
      fatal.store(true);
      // Each Batch::flush() only clears its columns *after* a successful
      // Insert (see e.g. OptionQuotesBatch::flush above), so whatever hasn't
      // been durably written yet is still sitting in `batches` right now.
      // Silently dropping it here would lose up to (batch_size - 1) rows per
      // table with no record it ever happened — retry the flush once so a
      // transient fault (e.g. one bad Insert) doesn't cost data that was
      // otherwise fine, and report exactly what's lost if it isn't.
      const std::size_t pending = batches.option_quotes.count + batches.underlyings.count +
                                  batches.chains.count + batches.underlying_quotes.count;
      std::string outcome;
      if (pending > 0) {
        try {
          batches.flush_all(client);
          outcome = " (" + std::to_string(pending) + " pending row(s) recovered on retry)";
        } catch (const std::exception& flush_err) {
          outcome = " (" + std::to_string(pending) +
                    " pending row(s) LOST — retry flush also failed: " + flush_err.what() + ")";
        }
      }
      std::lock_guard<std::mutex> lk(log_mutex);
      std::cerr << "[insert] worker aborted: " << e.what() << outcome << "\n";
    }
  };

  std::vector<std::thread> pool;
  pool.reserve(worker_count);
  for (std::size_t i = 0; i < worker_count; ++i) pool.emplace_back(worker);
  for (auto& t : pool) t.join();

  const auto elapsed = std::chrono::duration<double>(clock::now() - started).count();
  std::cout << "[insert] done in " << elapsed << "s: "
            << files_ok.load() << " ok, " << files_failed.load() << " failed, "
            << option_rows.load() << " option rows, "
            << options_skipped.load() << " option(s) skipped (invalid strike)\n";

  return !fatal.load() && files_failed.load() == 0;
}

}  // namespace ope

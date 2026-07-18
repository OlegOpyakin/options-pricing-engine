#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>
#include <thread>

namespace ope {

// ClickHouse connection settings + parser tuning, sourced from environment
// variables so the same binary works inside the dev container (host
// "clickhouse") and against a locally forwarded port (host "localhost").
struct ChConfig {
  std::string host = "localhost";
  uint16_t port = 9000;
  std::string database = "options_pricing";
  std::string user = "default";
  std::string password = "";

  // How many rows to accumulate before flushing a batch INSERT.
  std::size_t batch_size = 50000;
  // Number of parser/inserter worker threads.
  std::size_t workers = 0;  // 0 -> auto (hardware_concurrency)

  static ChConfig from_env() {
    ChConfig cfg;
    if (const char* v = std::getenv("CLICKHOUSE_HOST")) cfg.host = v;
    if (const char* v = std::getenv("CLICKHOUSE_PORT")) cfg.port = static_cast<uint16_t>(std::atoi(v));
    if (const char* v = std::getenv("CLICKHOUSE_DB")) cfg.database = v;
    if (const char* v = std::getenv("CLICKHOUSE_USER")) cfg.user = v;
    if (const char* v = std::getenv("CLICKHOUSE_PASSWORD")) cfg.password = v;
    if (const char* v = std::getenv("OPE_BATCH_SIZE")) cfg.batch_size = static_cast<std::size_t>(std::strtoull(v, nullptr, 10));
    if (const char* v = std::getenv("OPE_WORKERS")) cfg.workers = static_cast<std::size_t>(std::strtoull(v, nullptr, 10));

    if (cfg.workers == 0) {
      unsigned hw = std::thread::hardware_concurrency();
      cfg.workers = hw == 0 ? 4 : hw;
    }
    if (cfg.batch_size == 0) cfg.batch_size = 50000;
    return cfg;
  }
};

}  // namespace ope

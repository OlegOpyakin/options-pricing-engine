#pragma once

#include <string>

#include "ope/ch_config.hpp"

namespace ope {

// Imports CME ivcurve JSON data into ClickHouse.
//
// Usage from the main event loop:
//     Inserter inserter(ChConfig::from_env());
//     bool ok = inserter.insert("data/CME");        // directory (recursive)
//     bool ok = inserter.insert("data/CME/.../x.json");  // single file
//
// The path may point to a single file or a directory; directories are walked
// recursively and every *.json file is imported. Files are distributed to a
// pool of worker threads through a blocking queue; each worker parses files and
// streams rows into ClickHouse in batches (see ChConfig::batch_size).
//
// Returns true only if every discovered file was parsed and inserted without
// error. A human-readable summary is printed to stdout/stderr regardless.
class Inserter {
 public:
  explicit Inserter(ChConfig config);

  bool insert(const std::string& path);

 private:
  ChConfig config_;
};

}  // namespace ope

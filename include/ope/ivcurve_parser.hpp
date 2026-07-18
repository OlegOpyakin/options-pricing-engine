#pragma once

#include <optional>
#include <string>

#include "ope/parsed_rows.hpp"

namespace ope {

// Parses a single CME ivcurve JSON file into row structs ready for insertion.
// `source_file` is stored verbatim on every emitted row for lineage.
// Returns std::nullopt (and leaves `error` populated) if the file cannot be
// read or does not match the expected ivcurve shape.
std::optional<ParsedFile> parse_ivcurve_file(const std::string& path,
                                             std::string& error);

}  // namespace ope

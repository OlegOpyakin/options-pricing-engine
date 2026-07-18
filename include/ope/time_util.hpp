#pragma once

#include <cstdint>
#include <ctime>
#include <string_view>

namespace ope::timeutil {

// Parse an ISO-8601 UTC timestamp into nanoseconds since the Unix epoch.
// Accepts forms like:
//   "2026-05-25T14:59:01.800326283Z"  (up to 9 fractional digits)
//   "2026-05-25T14:58:59Z"            (no fraction)
//   "1970-01-01T00:00:00Z"            (epoch)
// Returns 0 for the epoch string and for anything it fails to parse.
int64_t parse_iso8601_ns(std::string_view s);

// Convert a CME expirationDate integer (YYYYMMDD, e.g. 20260529) to the number
// of days since the Unix epoch (what ClickHouse's Date column stores) and to
// midnight-UTC seconds (handy for ColumnDate::Append which takes seconds).
int32_t yyyymmdd_to_days(uint32_t yyyymmdd);
std::time_t yyyymmdd_to_seconds(uint32_t yyyymmdd);

}  // namespace ope::timeutil

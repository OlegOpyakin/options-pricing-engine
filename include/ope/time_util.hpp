#pragma once

#include <cstdint>
#include <ctime>
#include <optional>
#include <string_view>

namespace ope::timeutil {

// Parse an ISO-8601 UTC timestamp into nanoseconds since the Unix epoch.
// Accepts forms like:
//   "2026-05-25T14:59:01.800326283Z"  (up to 9 fractional digits)
//   "2026-05-25T14:58:59Z"            (no fraction)
//   "1970-01-01T00:00:00Z"            (epoch)
// The epoch string is a legitimate sentinel value in the source data (meaning
// "no quote yet") and parses to exactly 0. A malformed or unparseable string
// must not be confused with it, so failure is reported as std::nullopt rather
// than silently returned as 0 — callers decide whether a missing timestamp is
// fatal or tolerable, the parser must never guess.
std::optional<int64_t> parse_iso8601_ns(std::string_view s);

// Convert a CME expirationDate integer (YYYYMMDD, e.g. 20260529) to the number
// of days since the Unix epoch (what ClickHouse's Date column stores) and to
// midnight-UTC seconds (handy for ColumnDate::Append which takes seconds).
int32_t yyyymmdd_to_days(uint32_t yyyymmdd);
std::time_t yyyymmdd_to_seconds(uint32_t yyyymmdd);

}  // namespace ope::timeutil

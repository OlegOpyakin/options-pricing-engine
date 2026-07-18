#include "ope/time_util.hpp"

#include <cctype>
#include <cstdio>

namespace ope::timeutil {

int64_t parse_iso8601_ns(std::string_view s) {
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  // The leading date-time part is fixed width; sscanf is simple and fast here.
  if (std::sscanf(s.data(), "%4d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day, &hour,
                  &minute, &second) != 6) {
    return 0;
  }

  std::tm tm{};
  tm.tm_year = year - 1900;
  tm.tm_mon = month - 1;
  tm.tm_mday = day;
  tm.tm_hour = hour;
  tm.tm_min = minute;
  tm.tm_sec = second;
  // timegm interprets the struct as UTC (unlike mktime which uses local time).
  const std::time_t secs = timegm(&tm);
  int64_t ns = static_cast<int64_t>(secs) * 1'000'000'000LL;

  // Optional fractional seconds: ".<digits>" up to nanosecond precision.
  const auto dot = s.find('.');
  if (dot != std::string_view::npos) {
    int64_t frac = 0;
    int digits = 0;
    for (std::size_t i = dot + 1; i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])); ++i) {
      if (digits < 9) {
        frac = frac * 10 + (s[i] - '0');
        ++digits;
      }
    }
    // Scale to nanoseconds (pad missing low-order digits).
    for (; digits < 9; ++digits) frac *= 10;
    ns += frac;
  }
  return ns;
}

std::time_t yyyymmdd_to_seconds(uint32_t yyyymmdd) {
  const int year = static_cast<int>(yyyymmdd / 10000);
  const int month = static_cast<int>((yyyymmdd / 100) % 100);
  const int day = static_cast<int>(yyyymmdd % 100);

  std::tm tm{};
  tm.tm_year = year - 1900;
  tm.tm_mon = month - 1;
  tm.tm_mday = day;
  return timegm(&tm);
}

int32_t yyyymmdd_to_days(uint32_t yyyymmdd) {
  return static_cast<int32_t>(yyyymmdd_to_seconds(yyyymmdd) / 86400);
}

}  // namespace ope::timeutil

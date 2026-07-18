#include "ope/time_util.hpp"

#include <cctype>
#include <cstdio>

namespace ope::timeutil {

namespace {

bool is_leap_year(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int days_in_month(int month, int year) {
  static constexpr int kDaysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && is_leap_year(year)) return 29;
  return kDaysPerMonth[month - 1];
}

}  // namespace

std::optional<int64_t> parse_iso8601_ns(std::string_view s) {
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  // The leading date-time part is fixed width; sscanf is simple and fast here.
  // std::string_view::data() is not guaranteed NUL-terminated, but every
  // caller passes a std::string's .c_str()-backed view (see ivcurve_parser.cpp),
  // so this is safe here; sscanf also stops at %2d field widths regardless.
  if (std::sscanf(s.data(), "%4d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day, &hour,
                  &minute, &second) != 6) {
    return std::nullopt;
  }
  // sscanf only checks digit shape, not that the date/time is real (e.g. it
  // happily accepts month 13 or hour 99) — reject those explicitly instead of
  // letting timegm() silently normalize them into some other valid instant.
  if (month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 ||
      minute < 0 || minute > 59 || second < 0 || second > 60 /* leap second */) {
    return std::nullopt;
  }
  // month is already known valid here, so days_in_month's [1,12] indexing is safe.
  if (day > days_in_month(month, year)) {
    return std::nullopt;
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

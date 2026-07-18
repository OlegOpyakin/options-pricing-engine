#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

#include "ope/time_util.hpp"

using namespace ope::timeutil;

TEST_CASE("parses a timestamp with fractional seconds", "[time_util]") {
  const auto base = parse_iso8601_ns("2026-05-25T14:59:01Z");
  const auto with_frac = parse_iso8601_ns("2026-05-25T14:59:01.800326283Z");
  REQUIRE(base.has_value());
  REQUIRE(with_frac.has_value());
  REQUIRE(*with_frac == *base + 800326283LL);
}

TEST_CASE("parses a timestamp without a fraction", "[time_util]") {
  const auto ns = parse_iso8601_ns("2026-05-25T14:58:59Z");
  REQUIRE(ns.has_value());
  REQUIRE(*ns % 1'000'000'000LL == 0);
}

TEST_CASE("epoch sentinel parses to exactly 0, not an error", "[time_util]") {
  const auto ns = parse_iso8601_ns("1970-01-01T00:00:00Z");
  REQUIRE(ns.has_value());
  REQUIRE(*ns == 0);
}

TEST_CASE("malformed timestamp is a parse failure, not silently 0", "[time_util]") {
  REQUIRE_FALSE(parse_iso8601_ns("not-a-timestamp").has_value());
  REQUIRE_FALSE(parse_iso8601_ns("").has_value());
  REQUIRE_FALSE(parse_iso8601_ns("2026-13-99T99:99:99Z").has_value());
}

TEST_CASE("rejects a day that doesn't exist in the given month", "[time_util]") {
  // April, June, September, November only have 30 days.
  REQUIRE_FALSE(parse_iso8601_ns("2026-04-31T00:00:00Z").has_value());
  // February 30th never exists.
  REQUIRE_FALSE(parse_iso8601_ns("2026-02-30T00:00:00Z").has_value());
  // 2026 is not a leap year.
  REQUIRE_FALSE(parse_iso8601_ns("2026-02-29T00:00:00Z").has_value());
}

TEST_CASE("accepts February 29th in a leap year", "[time_util]") {
  const auto ns = parse_iso8601_ns("2024-02-29T00:00:00Z");
  REQUIRE(ns.has_value());
}

TEST_CASE("accepts the last real day of every month in a non-leap year", "[time_util]") {
  for (const auto& [ymd, expect_ok] : std::vector<std::pair<std::string, bool>>{
           {"2026-01-31T00:00:00Z", true}, {"2026-02-28T00:00:00Z", true},
           {"2026-03-31T00:00:00Z", true}, {"2026-04-30T00:00:00Z", true},
           {"2026-05-31T00:00:00Z", true}, {"2026-06-30T00:00:00Z", true},
           {"2026-07-31T00:00:00Z", true}, {"2026-08-31T00:00:00Z", true},
           {"2026-09-30T00:00:00Z", true}, {"2026-10-31T00:00:00Z", true},
           {"2026-11-30T00:00:00Z", true}, {"2026-12-31T00:00:00Z", true},
       }) {
    INFO("timestamp: " << ymd);
    CHECK(parse_iso8601_ns(ymd).has_value() == expect_ok);
  }
}

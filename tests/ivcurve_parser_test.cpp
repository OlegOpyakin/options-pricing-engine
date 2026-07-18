#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "ope/ivcurve_parser.hpp"

namespace {

std::string write_fixture(const std::string& name, const std::string& content) {
  const auto dir = std::filesystem::temp_directory_path() / "ope_parser_tests";
  std::filesystem::create_directories(dir);
  const auto path = dir / name;
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << content;
  return path.string();
}

std::string render(std::string tmpl,
                    const std::vector<std::pair<std::string, std::string>>& subs) {
  for (const auto& [key, value] : subs) {
    const std::string token = "{{" + key + "}}";
    std::size_t pos;
    while ((pos = tmpl.find(token)) != std::string::npos) {
      tmpl.replace(pos, token.size(), value);
    }
  }
  return tmpl;
}

// Shaped after the real CME_MINI-ESM2026-E2C-20260610.json sample: same field
// names/nesting, {{TOKEN}} placeholders for the values each test varies.
constexpr const char* kTemplate = R"JSON({
  "ivcurve": {
    "id": "CME_MINI:ESM2026;E2C;20260610",
    "now": "{{NOW}}",
    "expirationDate": 20260610,
    "exerciseStyle": "{{EXERCISE}}",
    "interestRate": 0.01,
    "underlyingType": "{{UTYPE}}",
    "isPayingDividends": false,
    "marketData": {
      "id": "CME_MINI:ESM2026;E2C;20260610",
      "time": "2026-05-07T09:54:18Z",
      "underlying": {
        "id": "{{UID}}",
        "mode": "realtime",
        "time": "2026-05-07T09:54:18Z",
        "bid": {"price": 7392.5, "size": 23, "time": "2026-05-07T09:54:18Z"},
        "ask": {"price": 7392.75, "size": 6, "time": "2026-05-07T09:54:18Z"},
        "last": {"price": 7392.75, "size": 5, "time": "2026-05-07T09:54:18Z"}
      },
      "puts": {
        "{{PUT_STRIKE}}": {
          "id": "CME_MINI:E2C260610P4500",
          "mode": "{{PUT_MODE}}",
          "time": "2026-05-07T09:25:41Z",
          "bid": {"price": 0.5, "size": 168, "time": "2026-05-07T09:25:41Z"},
          "ask": {"price": 0.85, "size": 168, "time": "2026-05-07T09:25:41Z"},
          "last": null
        }
      },
      "calls": {
        "4600.": {
          "id": "CME_MINI:E2C260610C4600",
          "mode": "snapshot",
          "time": "1970-01-01T00:00:00Z",
          "bid": null,
          "ask": null,
          "last": null
        }
      }
    }
  }
})JSON";

std::string default_valid_json() {
  return render(kTemplate, {
                                {"NOW", "2026-05-07T09:54:21.752997491Z"},
                                {"EXERCISE", "european"},
                                {"UTYPE", "futures"},
                                {"UID", "CME_MINI:ESM2026"},
                                {"PUT_STRIKE", "4500."},
                                {"PUT_MODE", "realtime"},
                            });
}

}  // namespace

TEST_CASE("parses a valid file matching the real ivcurve shape", "[parser][valid]") {
  const auto path = write_fixture("valid.json", default_valid_json());
  std::string error;
  const auto parsed = ope::parse_ivcurve_file(path, error);

  REQUIRE(parsed.has_value());
  CHECK(parsed->underlying.underlying_id == "CME_MINI:ESM2026");
  CHECK(parsed->chain.exercise_style == "european");
  CHECK(parsed->options.size() == 2);
}

TEST_CASE("captures marketData.id and marketData.time distinctly from ivcurve.id/now",
          "[parser][marketdata]") {
  const auto path = write_fixture("marketdata.json", default_valid_json());
  std::string error;
  const auto parsed = ope::parse_ivcurve_file(path, error);

  REQUIRE(parsed.has_value());
  CHECK(parsed->market_data_id == "CME_MINI:ESM2026;E2C;20260610");
  // marketData.time (09:54:18Z) differs from ivcurve.now (09:54:21...Z) in
  // the fixture, exactly as it does in the real sample file.
  CHECK(parsed->market_data_time_ns != parsed->chain.snapshot_ns);
  CHECK(parsed->market_data_time_ns != 0);
}

TEST_CASE("rejects a file whose marketData.underlying.id disagrees with ivcurve.id",
          "[parser][marketdata]") {
  const auto json = render(kTemplate, {
                                           {"NOW", "2026-05-07T09:54:21.752997491Z"},
                                           {"EXERCISE", "european"},
                                           {"UTYPE", "futures"},
                                           {"UID", "CME_MINI:SOMETHING_ELSE"},
                                           {"PUT_STRIKE", "4500."},
                                           {"PUT_MODE", "realtime"},
                                       });
  const auto path = write_fixture("mismatched_underlying_id.json", json);
  std::string error;
  const auto parsed = ope::parse_ivcurve_file(path, error);

  REQUIRE_FALSE(parsed.has_value());
  CHECK(error.find("underlying") != std::string::npos);
}

// A single invalid strike must not discard the rest of an otherwise valid
// file — real data has files where one degenerate placeholder entry
// (e.g. strike "0.", no live quotes) sits alongside genuinely-quoted
// strikes. The bad entry is skipped and recorded in ParsedFile::skipped_options
// instead of failing the whole file. See parser_specification.md §25.
TEST_CASE("skips (not fails) a strike key with trailing garbage", "[parser][strike]") {
  const auto json = render(kTemplate, {
                                           {"NOW", "2026-05-07T09:54:21.752997491Z"},
                                           {"EXERCISE", "european"},
                                           {"UTYPE", "futures"},
                                           {"UID", "CME_MINI:ESM2026"},
                                           {"PUT_STRIKE", "170abc"},
                                           {"PUT_MODE", "realtime"},
                                       });
  const auto path = write_fixture("strike_garbage.json", json);
  std::string error;
  const auto parsed = ope::parse_ivcurve_file(path, error);

  REQUIRE(parsed.has_value());
  CHECK(parsed->options.size() == 1);  // only the valid call remains
  REQUIRE(parsed->skipped_options.size() == 1);
  CHECK(parsed->skipped_options[0].find("170abc") != std::string::npos);
}

TEST_CASE("skips (not fails) a zero or negative strike key", "[parser][strike]") {
  for (const std::string& bad_strike : {"0.", "-5."}) {
    const auto json = render(kTemplate, {
                                             {"NOW", "2026-05-07T09:54:21.752997491Z"},
                                             {"EXERCISE", "european"},
                                             {"UTYPE", "futures"},
                                             {"UID", "CME_MINI:ESM2026"},
                                             {"PUT_STRIKE", bad_strike},
                                             {"PUT_MODE", "realtime"},
                                         });
    const auto path = write_fixture("strike_nonpositive.json", json);
    std::string error;
    const auto parsed = ope::parse_ivcurve_file(path, error);
    INFO("strike key: " << bad_strike);

    REQUIRE(parsed.has_value());
    CHECK(parsed->options.size() == 1);
    REQUIRE(parsed->skipped_options.size() == 1);
    CHECK(parsed->skipped_options[0].find(bad_strike) != std::string::npos);
  }
}

TEST_CASE("skips (not fails) a non-finite strike key", "[parser][strike]") {
  const auto json = render(kTemplate, {
                                           {"NOW", "2026-05-07T09:54:21.752997491Z"},
                                           {"EXERCISE", "european"},
                                           {"UTYPE", "futures"},
                                           {"UID", "CME_MINI:ESM2026"},
                                           {"PUT_STRIKE", "nan"},
                                           {"PUT_MODE", "realtime"},
                                       });
  const auto path = write_fixture("strike_nan.json", json);
  std::string error;
  const auto parsed = ope::parse_ivcurve_file(path, error);

  REQUIRE(parsed.has_value());
  CHECK(parsed->options.size() == 1);
  REQUIRE(parsed->skipped_options.size() == 1);
  CHECK(parsed->skipped_options[0].find("nan") != std::string::npos);
}

TEST_CASE("rejects an unsupported exercise style", "[parser][enum]") {
  const auto json = render(kTemplate, {
                                           {"NOW", "2026-05-07T09:54:21.752997491Z"},
                                           {"EXERCISE", "bermudan"},
                                           {"UTYPE", "futures"},
                                           {"UID", "CME_MINI:ESM2026"},
                                           {"PUT_STRIKE", "4500."},
                                           {"PUT_MODE", "realtime"},
                                       });
  const auto path = write_fixture("bad_exercise_style.json", json);
  std::string error;
  const auto parsed = ope::parse_ivcurve_file(path, error);
  REQUIRE_FALSE(parsed.has_value());
}

TEST_CASE("rejects an unsupported underlying type", "[parser][enum]") {
  const auto json = render(kTemplate, {
                                           {"NOW", "2026-05-07T09:54:21.752997491Z"},
                                           {"EXERCISE", "european"},
                                           {"UTYPE", "bond"},
                                           {"UID", "CME_MINI:ESM2026"},
                                           {"PUT_STRIKE", "4500."},
                                           {"PUT_MODE", "realtime"},
                                       });
  const auto path = write_fixture("bad_underlying_type.json", json);
  std::string error;
  const auto parsed = ope::parse_ivcurve_file(path, error);
  REQUIRE_FALSE(parsed.has_value());
}

TEST_CASE("accepts every underlyingType value observed in the real full dataset",
          "[parser][enum]") {
  // stock/fund/futures/dr/index — measured across all 30564 real sample
  // files (data/README.md exchanges). Note "etf" never appears literally;
  // ETFs are represented as "fund" in this source. See
  // parser_specification.md §24/§25 for the discrepancy with the original
  // docx description.
  for (const std::string& utype : {"stock", "fund", "futures", "dr", "index"}) {
    const auto json = render(kTemplate, {
                                             {"NOW", "2026-05-07T09:54:21.752997491Z"},
                                             {"EXERCISE", "european"},
                                             {"UTYPE", utype},
                                             {"UID", "CME_MINI:ESM2026"},
                                             {"PUT_STRIKE", "4500."},
                                             {"PUT_MODE", "realtime"},
                                         });
    const auto path = write_fixture("utype_" + utype + ".json", json);
    std::string error;
    const auto parsed = ope::parse_ivcurve_file(path, error);
    INFO("underlyingType: " << utype);
    REQUIRE(parsed.has_value());
  }
}

TEST_CASE("rejects an unsupported quote mode", "[parser][enum]") {
  const auto json = render(kTemplate, {
                                           {"NOW", "2026-05-07T09:54:21.752997491Z"},
                                           {"EXERCISE", "european"},
                                           {"UTYPE", "futures"},
                                           {"UID", "CME_MINI:ESM2026"},
                                           {"PUT_STRIKE", "4500."},
                                           {"PUT_MODE", "delayed"},
                                       });
  const auto path = write_fixture("bad_mode.json", json);
  std::string error;
  const auto parsed = ope::parse_ivcurve_file(path, error);
  REQUIRE_FALSE(parsed.has_value());
}

TEST_CASE("rejects a malformed ivcurve.now timestamp instead of defaulting to epoch",
          "[parser][timestamp]") {
  const auto json = render(kTemplate, {
                                           {"NOW", "not-a-timestamp"},
                                           {"EXERCISE", "european"},
                                           {"UTYPE", "futures"},
                                           {"UID", "CME_MINI:ESM2026"},
                                           {"PUT_STRIKE", "4500."},
                                           {"PUT_MODE", "realtime"},
                                       });
  const auto path = write_fixture("bad_now.json", json);
  std::string error;
  const auto parsed = ope::parse_ivcurve_file(path, error);
  REQUIRE_FALSE(parsed.has_value());
}

TEST_CASE("rejects a file missing a required top-level field", "[parser][required]") {
  constexpr const char* kMissingExerciseStyle = R"JSON({
    "ivcurve": {
      "id": "CME_MINI:ESM2026;E2C;20260610",
      "now": "2026-05-07T09:54:21.752997491Z",
      "expirationDate": 20260610,
      "interestRate": 0.01,
      "underlyingType": "futures",
      "isPayingDividends": false,
      "marketData": {
        "id": "CME_MINI:ESM2026;E2C;20260610",
        "time": "2026-05-07T09:54:18Z",
        "underlying": {
          "id": "CME_MINI:ESM2026", "mode": "realtime",
          "time": "2026-05-07T09:54:18Z",
          "bid": null, "ask": null, "last": null
        },
        "puts": {},
        "calls": {}
      }
    }
  })JSON";
  const auto path = write_fixture("missing_exercise_style.json", kMissingExerciseStyle);
  std::string error;
  const auto parsed = ope::parse_ivcurve_file(path, error);
  REQUIRE_FALSE(parsed.has_value());
}

TEST_CASE("rejects a file missing the calls collection entirely", "[parser][required]") {
  constexpr const char* kMissingCalls = R"JSON({
    "ivcurve": {
      "id": "CME_MINI:ESM2026;E2C;20260610",
      "now": "2026-05-07T09:54:21.752997491Z",
      "expirationDate": 20260610,
      "exerciseStyle": "european",
      "interestRate": 0.01,
      "underlyingType": "futures",
      "isPayingDividends": false,
      "marketData": {
        "id": "CME_MINI:ESM2026;E2C;20260610",
        "time": "2026-05-07T09:54:18Z",
        "underlying": {
          "id": "CME_MINI:ESM2026", "mode": "realtime",
          "time": "2026-05-07T09:54:18Z",
          "bid": null, "ask": null, "last": null
        },
        "puts": {}
      }
    }
  })JSON";
  const auto path = write_fixture("missing_calls.json", kMissingCalls);
  std::string error;
  const auto parsed = ope::parse_ivcurve_file(path, error);
  REQUIRE_FALSE(parsed.has_value());
}

TEST_CASE("skips a doubly-malformed entry (bad strike AND non-object value) instead of "
          "failing the whole file",
          "[parser][strike][regression]") {
  // entry.value("id", ...) throws if the entry isn't a JSON object at all —
  // guard against that so a doubly-broken record still only costs itself,
  // not every valid strike elsewhere in the file (see §25.2's "skip not
  // fail" guarantee).
  constexpr const char* kDoublyBroken = R"JSON({
    "ivcurve": {
      "id": "CME_MINI:ESM2026;E2C;20260610",
      "now": "2026-05-07T09:54:21.752997491Z",
      "expirationDate": 20260610,
      "exerciseStyle": "european",
      "interestRate": 0.01,
      "underlyingType": "futures",
      "isPayingDividends": false,
      "marketData": {
        "id": "CME_MINI:ESM2026;E2C;20260610",
        "time": "2026-05-07T09:54:18Z",
        "underlying": {
          "id": "CME_MINI:ESM2026", "mode": "realtime",
          "time": "2026-05-07T09:54:18Z",
          "bid": null, "ask": null, "last": null
        },
        "puts": {
          "0.": "not an object at all",
          "4500.": {
            "id": "CME_MINI:E2C260610P4500", "mode": "realtime",
            "time": "2026-05-07T09:25:41Z",
            "bid": {"price": 0.5, "size": 168, "time": "2026-05-07T09:25:41Z"},
            "ask": {"price": 0.85, "size": 168, "time": "2026-05-07T09:25:41Z"},
            "last": null
          }
        },
        "calls": {}
      }
    }
  })JSON";
  const auto path = write_fixture("doubly_broken.json", kDoublyBroken);
  std::string error;
  const auto parsed = ope::parse_ivcurve_file(path, error);

  REQUIRE(parsed.has_value());
  CHECK(parsed->options.size() == 1);
  REQUIRE(parsed->skipped_options.size() == 1);
  CHECK(parsed->skipped_options[0].find("0.") != std::string::npos);
}

TEST_CASE("preserves null bid/ask/last and the epoch sentinel without correction",
          "[parser][regression]") {
  const auto path = write_fixture("epoch_and_nulls.json", default_valid_json());
  std::string error;
  const auto parsed = ope::parse_ivcurve_file(path, error);

  REQUIRE(parsed.has_value());
  const auto& call = parsed->options[std::string_view(parsed->options[0].option_id).find("C4600") !=
                                              std::string_view::npos
                                          ? 0
                                          : 1];
  CHECK_FALSE(call.bid.price.has_value());
  CHECK_FALSE(call.ask.price.has_value());
  CHECK_FALSE(call.last.price.has_value());
  REQUIRE(call.quote_time_ns.has_value());
  CHECK(*call.quote_time_ns == 0);  // 1970-01-01T00:00:00Z sentinel, preserved verbatim
}

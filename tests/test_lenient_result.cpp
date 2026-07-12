// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#include <doctest.h>

#include <nlohmann/json.hpp>

#include "internal/json_parse.hpp"

using audd::internal::result_array_or_null;

// On status=success with a wrong-typed `result`, the list endpoints
// (recognize_enterprise, streams().list, advanced().find_lyrics) must degrade
// to an empty result set rather than throw. result_array_or_null is the shared
// guard those endpoints use; it returns nullptr for anything that is not a
// JSON array, so the caller yields an empty vector.

TEST_CASE("result_array_or_null degrades wrong-typed result to empty") {
    auto boolean = nlohmann::json::parse(R"({"status":"success","result":true})");
    CHECK(result_array_or_null(boolean) == nullptr);

    auto obj = nlohmann::json::parse(R"({"status":"success","result":{"a":1}})");
    CHECK(result_array_or_null(obj) == nullptr);

    auto str = nlohmann::json::parse(R"({"status":"success","result":"nope"})");
    CHECK(result_array_or_null(str) == nullptr);

    auto num = nlohmann::json::parse(R"({"status":"success","result":42})");
    CHECK(result_array_or_null(num) == nullptr);
}

TEST_CASE("result_array_or_null treats absent/null result as empty") {
    auto absent = nlohmann::json::parse(R"({"status":"success"})");
    CHECK(result_array_or_null(absent) == nullptr);

    auto null_result = nlohmann::json::parse(R"({"status":"success","result":null})");
    CHECK(result_array_or_null(null_result) == nullptr);
}

TEST_CASE("result_array_or_null returns the array when result is a list") {
    auto arr = nlohmann::json::parse(R"({"status":"success","result":[{"x":1},{"x":2}]})");
    const auto* p = result_array_or_null(arr);
    REQUIRE(p != nullptr);
    CHECK(p->is_array());
    CHECK(p->size() == 2);

    auto empty = nlohmann::json::parse(R"({"status":"success","result":[]})");
    const auto* pe = result_array_or_null(empty);
    REQUIRE(pe != nullptr);
    CHECK(pe->empty());
}

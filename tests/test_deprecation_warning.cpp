// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)
//
// A code-51 warning on a successful response reaches on_deprecation, and the
// result stays usable.

#include <doctest.h>

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "internal/client_internal.hpp"
#include "internal/http_client.hpp"
#include "internal/json_parse.hpp"
#include "test_support.hpp"

using audd::internal::decode_or_throw;
using audd::internal::HttpResponse;

namespace {

HttpResponse response(const std::string& raw) {
    HttpResponse r;
    r.http_status = 200;
    r.raw_body = raw;
    r.json_body = nlohmann::json::parse(raw);
    return r;
}

} // namespace

TEST_CASE("top-level code-51 warning on a success body reaches on_deprecation") {
    auto resp = response(audd_test::read_fixture("recognize_warning_51.json"));
    std::vector<std::string> messages;
    auto body = decode_or_throw(resp, [&](const std::string& m) { messages.push_back(m); });
    REQUIRE(messages.size() == 1);
    CHECK(messages[0].find("deprecated or nonexistent parameters") != std::string::npos);
    auto r = audd::internal::parse_recognition(body["result"]);
    CHECK(r.artist == "Tears For Fears");
    CHECK(r.title == "Everybody Wants To Rule The World");
}

TEST_CASE("code-51 error block next to a result also reaches on_deprecation") {
    auto resp = response(R"({"status":"error","error":{"error_code":51,"error_message":"old param"},
                             "result":{"artist":"A","title":"T"}})");
    std::vector<std::string> messages;
    auto body = decode_or_throw(resp, [&](const std::string& m) { messages.push_back(m); });
    CHECK(messages == std::vector<std::string>{"old param"});
    CHECK(body["result"]["artist"] == "A");
}

TEST_CASE("a success body without a code-51 warning does not call on_deprecation") {
    int calls = 0;
    auto on_dep = [&](const std::string&) { ++calls; };
    auto plain = response(R"({"status":"success","result":null})");
    decode_or_throw(plain, on_dep);
    auto other = response(R"({"status":"success","result":null,
                              "warning":{"error_code":52,"error_message":"something else"}})");
    decode_or_throw(other, on_dep);
    auto junk = response(R"({"status":"success","result":null,"warning":"text"})");
    decode_or_throw(junk, on_dep);
    CHECK(calls == 0);
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#include <doctest.h>

#include <string>

#include "internal/json_parse.hpp"

using audd::internal::url_query_has_key;

// The `return` collision check on a callback URL must match whole query keys
// only: a caller's own `?_return=1` (or `?myreturn=1`) must NOT be flagged,
// while a genuine `?return=...` (in any query position) MUST be.

TEST_CASE("url_query_has_key does not false-positive on similar keys") {
    CHECK_FALSE(url_query_has_key("https://cb.example/hook?_return=1", "return"));
    CHECK_FALSE(url_query_has_key("https://cb.example/hook?myreturn=1", "return"));
    CHECK_FALSE(url_query_has_key("https://cb.example/hook?returned=1", "return"));
    CHECK_FALSE(url_query_has_key("https://cb.example/hook?a=return", "return"));
    CHECK_FALSE(url_query_has_key("https://cb.example/hook", "return"));
    CHECK_FALSE(url_query_has_key("https://cb.example/return", "return"));
}

TEST_CASE("url_query_has_key detects a real return key in any position") {
    CHECK(url_query_has_key("https://cb.example/hook?return=1", "return"));
    CHECK(url_query_has_key("https://cb.example/hook?a=b&return=c", "return"));
    CHECK(url_query_has_key("https://cb.example/hook?return=apple_music,spotify", "return"));
    CHECK(url_query_has_key("https://cb.example/hook?x=1&return=2&y=3", "return"));
    // Key present with no value still counts as the key being set.
    CHECK(url_query_has_key("https://cb.example/hook?return", "return"));
    CHECK(url_query_has_key("https://cb.example/hook?a=1&return", "return"));
}

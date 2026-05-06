// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#include <doctest.h>

#include <audd/error.hpp>

TEST_CASE("sentinel_for_code maps known codes") {
    using audd::sentinel_for_code;
    using audd::ErrorCategory;

    // Authentication
    CHECK(sentinel_for_code(900) == ErrorCategory::Authentication);
    CHECK(sentinel_for_code(901) == ErrorCategory::Authentication);
    CHECK(sentinel_for_code(903) == ErrorCategory::Authentication);
    // Quota
    CHECK(sentinel_for_code(902) == ErrorCategory::Quota);
    // Subscription
    CHECK(sentinel_for_code(904) == ErrorCategory::Subscription);
    CHECK(sentinel_for_code(905) == ErrorCategory::Subscription);
    // InvalidRequest
    CHECK(sentinel_for_code(50)  == ErrorCategory::InvalidRequest);
    CHECK(sentinel_for_code(51)  == ErrorCategory::InvalidRequest);
    CHECK(sentinel_for_code(600) == ErrorCategory::InvalidRequest);
    CHECK(sentinel_for_code(601) == ErrorCategory::InvalidRequest);
    CHECK(sentinel_for_code(602) == ErrorCategory::InvalidRequest);
    CHECK(sentinel_for_code(700) == ErrorCategory::InvalidRequest);
    CHECK(sentinel_for_code(701) == ErrorCategory::InvalidRequest);
    CHECK(sentinel_for_code(702) == ErrorCategory::InvalidRequest);
    CHECK(sentinel_for_code(906) == ErrorCategory::InvalidRequest);
    // InvalidAudio
    CHECK(sentinel_for_code(300) == ErrorCategory::InvalidAudio);
    CHECK(sentinel_for_code(400) == ErrorCategory::InvalidAudio);
    CHECK(sentinel_for_code(500) == ErrorCategory::InvalidAudio);
    // RateLimit / StreamLimit
    CHECK(sentinel_for_code(610) == ErrorCategory::StreamLimit);
    CHECK(sentinel_for_code(611) == ErrorCategory::RateLimit);
    // NotReleased
    CHECK(sentinel_for_code(907) == ErrorCategory::NotReleased);
    // Blocked
    CHECK(sentinel_for_code(19)    == ErrorCategory::Blocked);
    CHECK(sentinel_for_code(31337) == ErrorCategory::Blocked);
    // NeedsUpdate
    CHECK(sentinel_for_code(20) == ErrorCategory::NeedsUpdate);
    // Server (default)
    CHECK(sentinel_for_code(0)    == ErrorCategory::Server);
    CHECK(sentinel_for_code(9999) == ErrorCategory::Server);
}

TEST_CASE("AuddApiError reports its category") {
    audd::AuddApiError e(900, "bad token", 401, "req-123");
    CHECK(e.category() == audd::ErrorCategory::Authentication);
    CHECK(e.error_code == 900);
    CHECK(e.message == "bad token");
    CHECK(e.http_status == 401);
    CHECK(e.request_id == "req-123");
}

TEST_CASE("AuddCustomCatalogAccessError uses CustomCatalogAccess category") {
    audd::AuddCustomCatalogAccessError e(905, "no enterprise", 403, "req-9");
    CHECK(e.category() == audd::ErrorCategory::CustomCatalogAccess);
    CHECK(e.server_message == "no enterprise");
    // The user-facing what() string should mention custom catalog footgun.
    std::string what = e.what();
    CHECK(what.find("custom catalog") != std::string::npos);
}

TEST_CASE("AuddConnectionError has Connection category") {
    audd::AuddConnectionError e("dns failure");
    CHECK(e.category() == audd::ErrorCategory::Connection);
}

TEST_CASE("AuddSerializationError has Serialization category") {
    audd::AuddSerializationError e("bad json", "raw");
    CHECK(e.category() == audd::ErrorCategory::Serialization);
    CHECK(e.raw_text == "raw");
}

TEST_CASE("category_name returns stable label") {
    CHECK(std::string(audd::category_name(audd::ErrorCategory::Authentication)) == "authentication");
    CHECK(std::string(audd::category_name(audd::ErrorCategory::RateLimit)) == "rate_limit");
}

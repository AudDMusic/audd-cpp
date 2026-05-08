// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)
//
// Verifies the retry invariant used by metered endpoints
// (custom_catalog().add): with max_attempts=1, retry_on_connection_error
// must invoke the callable exactly once regardless of whether it succeeds
// or throws AudDConnectionError. A 5xx HTTP response must also be a
// single attempt, because retry happens at the AudDConnectionError layer
// — HTTP-status retry is intentionally not performed.

#include <doctest.h>

#include <chrono>
#include <stdexcept>

#include <audd/error.hpp>
#include "internal/retry.hpp"

using audd::internal::retry_on_connection_error;

TEST_CASE("retry_on_connection_error: max_attempts=1 with returned response runs exactly once") {
    int attempts = 0;
    // Simulate a 5xx-style return: the HTTP layer returns a populated
    // response (no AudDConnectionError thrown), so the retry helper must
    // not loop. Callers translate the 5xx into an AudDApiError after the
    // helper returns.
    int http_status = retry_on_connection_error(
        /*max_attempts=*/1,
        std::chrono::milliseconds(0),
        [&]() -> int {
            ++attempts;
            return 503; // Service Unavailable
        });
    CHECK(attempts == 1);
    CHECK(http_status == 503);
}

TEST_CASE("retry_on_connection_error: max_attempts=1 with connect error rethrows after one call") {
    int attempts = 0;
    bool caught = false;
    try {
        retry_on_connection_error(
            /*max_attempts=*/1,
            std::chrono::milliseconds(0),
            [&]() -> int {
                ++attempts;
                throw audd::AudDConnectionError("simulated pre-upload connect failure");
            });
    } catch (const audd::AudDConnectionError& e) {
        caught = true;
        CHECK(std::string(e.what()).find("connect") != std::string::npos);
    }
    CHECK(caught);
    CHECK(attempts == 1);
}

TEST_CASE("retry_on_connection_error: max_attempts=3 retries on connect error then rethrows") {
    int attempts = 0;
    bool caught = false;
    try {
        retry_on_connection_error(
            /*max_attempts=*/3,
            std::chrono::milliseconds(0),
            [&]() -> int {
                ++attempts;
                throw audd::AudDConnectionError("dns failure");
            });
    } catch (const audd::AudDConnectionError&) {
        caught = true;
    }
    CHECK(caught);
    CHECK(attempts == 3);
}

TEST_CASE("retry_on_connection_error: non-connection exception is not retried") {
    int attempts = 0;
    bool caught = false;
    try {
        retry_on_connection_error(
            /*max_attempts=*/5,
            std::chrono::milliseconds(0),
            [&]() -> int {
                ++attempts;
                // Any non-AudDConnectionError exception (e.g. AudDApiError
                // raised on a 5xx response after decode) must propagate
                // without retry.
                throw audd::AudDApiError(0, "internal", 500, "");
            });
    } catch (const audd::AudDApiError&) {
        caught = true;
    }
    CHECK(caught);
    CHECK(attempts == 1);
}

TEST_CASE("retry_on_connection_error: max_attempts<1 is clamped to 1") {
    int attempts = 0;
    bool caught = false;
    try {
        retry_on_connection_error(
            /*max_attempts=*/0,
            std::chrono::milliseconds(0),
            [&]() -> int {
                ++attempts;
                throw audd::AudDConnectionError("boom");
            });
    } catch (const audd::AudDConnectionError&) {
        caught = true;
    }
    CHECK(caught);
    CHECK(attempts == 1);
}

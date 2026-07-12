// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#include <doctest.h>

#include <nlohmann/json.hpp>

#include "internal/json_parse.hpp"

using audd::internal::classify_longpoll_response;
using audd::internal::LongpollDisposition;

// The longpoll worker classifies each completed HTTP round-trip before parsing
// the body. Only explicit HTTP/API errors are terminal; keep-alives advance
// and continue; empty bodies are skipped; anything else is a candidate event
// (an unparseable event body is then skipped by the worker, not terminal).

TEST_CASE("classify_longpoll_response: HTTP >= 400 is terminal") {
    nlohmann::json body;
    CHECK(classify_longpoll_response(400, body, true) == LongpollDisposition::Terminal);
    CHECK(classify_longpoll_response(401, body, false) == LongpollDisposition::Terminal);
    CHECK(classify_longpoll_response(500,
            nlohmann::json::parse(R"({"status":"error"})"), false)
          == LongpollDisposition::Terminal);
}

TEST_CASE("classify_longpoll_response: empty body is skipped, not terminal") {
    nlohmann::json null_body; // null
    CHECK(classify_longpoll_response(200, null_body, true) == LongpollDisposition::Skip);
}

TEST_CASE("classify_longpoll_response: keep-alive tick continues") {
    auto body = nlohmann::json::parse(R"({"timeout":50,"timestamp":1700000000})");
    CHECK(classify_longpoll_response(200, body, false) == LongpollDisposition::KeepAlive);
}

TEST_CASE("classify_longpoll_response: event bodies are candidates") {
    auto match = nlohmann::json::parse(R"({"result":{"radio_id":1}})");
    CHECK(classify_longpoll_response(200, match, false) == LongpollDisposition::Event);

    auto notif = nlohmann::json::parse(R"({"notification":{"radio_id":1}})");
    CHECK(classify_longpoll_response(200, notif, false) == LongpollDisposition::Event);

    // Even an odd/unknown non-empty 2xx body is a candidate event — the worker
    // then skips it if it fails to parse, keeping the subscription alive.
    auto unknown = nlohmann::json::parse(R"({"weird":123})");
    CHECK(classify_longpoll_response(200, unknown, false) == LongpollDisposition::Event);
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)
//
// parse_longpoll_response turns one longpoll body into the events to deliver,
// the next since_time, or a terminal error. The fixtures are copies of the
// shared audd-openapi fixtures.

#include <doctest.h>

#include <chrono>
#include <string>
#include <variant>

#include <nlohmann/json.hpp>

#include <audd/error.hpp>
#include "internal/http_client.hpp"
#include "internal/longpoll_internal.hpp"
#include "test_support.hpp"

using audd::internal::HttpResponse;
using audd::internal::longpoll_backoff_delay;
using audd::internal::LongpollPage;
using audd::internal::parse_longpoll_response;
using audd_test::read_fixture;

namespace {

HttpResponse response(const std::string& raw, int status = 200) {
    HttpResponse r;
    r.http_status = status;
    r.raw_body = raw;
    try { r.json_body = nlohmann::json::parse(raw); } catch (const std::exception&) {}
    return r;
}

const audd::StreamCallbackMatch& match_at(const LongpollPage& page, std::size_t i) {
    REQUIRE(i < page.events.size());
    const auto* m = std::get_if<audd::StreamCallbackMatch>(&page.events[i]);
    REQUIRE(m != nullptr);
    return *m;
}

} // namespace

TEST_CASE("longpoll events fixture: two songs in order, newest timestamp is next since_time") {
    auto page = parse_longpoll_response(response(read_fixture("longpoll_events.json")));
    CHECK_FALSE(page.error);
    CHECK(page.events_shape);
    REQUIRE(page.events.size() == 2);

    const auto& first = match_at(page, 0);
    CHECK(first.radio_id == 7);
    CHECK(first.song.artist == "Alan Walker, A$AP Rocky");
    CHECK(first.song.title == "Live Fast (PUBGM)");
    CHECK(first.timestamp == "2026-11-24 19:10:11");
    CHECK(first.play_length == 111);

    const auto& second = match_at(page, 1);
    CHECK(second.radio_id == 7);
    CHECK(second.song.artist == "Tears For Fears");
    CHECK(second.song.title == "Everybody Wants To Rule The World");
    CHECK(second.song.score == 100);

    REQUIRE(page.since.has_value());
    CHECK(*page.since == 1795547548112LL);
}

TEST_CASE("longpoll timeout fixture: nothing to deliver, its timestamp is next since_time") {
    auto page = parse_longpoll_response(response(read_fixture("longpoll_no_events.json")));
    CHECK_FALSE(page.error);
    CHECK(page.events.empty());
    REQUIRE(page.since.has_value());
    CHECK(*page.since == 1777901270049LL);
}

TEST_CASE("longpoll error fixture: AudDInvalidRequestError with the server's text") {
    const std::string raw = read_fixture("longpoll_error.json");
    auto page = parse_longpoll_response(response(raw));
    REQUIRE(page.error);
    CHECK(page.events.empty());
    try {
        std::rethrow_exception(page.error);
    } catch (const audd::AudDInvalidRequestError& e) {
        CHECK(e.category() == audd::ErrorCategory::InvalidRequest);
        CHECK(e.message == "Invalid timeout arg.  Must be 1-120.");
        CHECK(e.error_code == 0);
        CHECK(e.raw_response == raw);
    } catch (...) {
        FAIL("expected AudDInvalidRequestError");
    }
}

TEST_CASE("longpoll error object maps by its AudD code") {
    auto page = parse_longpoll_response(response(
        R"({"status":"error","error":{"error_code":900,"error_message":"Wrong API token"}})"));
    REQUIRE(page.error);
    try {
        std::rethrow_exception(page.error);
    } catch (const audd::AudDApiError& e) {
        CHECK(e.error_code == 900);
        CHECK(e.category() == audd::ErrorCategory::Authentication);
    } catch (...) {
        FAIL("expected AudDApiError");
    }
}

TEST_CASE("longpoll event data may be a JSON string, and notifications are delivered") {
    const auto fixture = nlohmann::json::parse(read_fixture("longpoll_events.json"));
    const auto notification =
        nlohmann::json::parse(read_fixture("streams_callback_with_notification.json"));
    nlohmann::json body;
    body["events"] = nlohmann::json::array({
        {{"timestamp", 10}, {"data", fixture["events"][0]["data"].dump()}},
        {{"timestamp", 20}, {"data", notification}},
    });
    auto page = parse_longpoll_response(response(body.dump()));
    REQUIRE(page.events.size() == 2);
    CHECK(match_at(page, 0).song.title == "Live Fast (PUBGM)");
    const auto* n = std::get_if<audd::StreamCallbackNotification>(&page.events[1]);
    REQUIRE(n != nullptr);
    CHECK(n->radio_id == 3);
    CHECK(n->notification_code == 650);
    REQUIRE(page.since.has_value());
    CHECK(*page.since == 20);
}

TEST_CASE("longpoll events that don't parse or have no songs are skipped") {
    auto page = parse_longpoll_response(response(R"({"events":[
        {"timestamp": 1, "data": {"status":"success","result":{"radio_id":1,"results":[]}}},
        {"timestamp": 2, "data": "not json"},
        {"timestamp": 3, "data": 42},
        {"timestamp": 4},
        "junk",
        {"timestamp": "5", "data": {"status":"success","result":{"radio_id":2,
            "results":[{"artist":"A","title":"T","score":90}]}}}
    ]})"));
    CHECK_FALSE(page.error);
    REQUIRE(page.events.size() == 1);
    CHECK(match_at(page, 0).radio_id == 2);
    CHECK(match_at(page, 0).song.artist == "A");
    REQUIRE(page.since.has_value());
    CHECK(*page.since == 5); // numeric strings are read too
}

TEST_CASE("longpoll empty events list is nothing new") {
    auto page = parse_longpoll_response(response(R"({"events":[]})"));
    CHECK_FALSE(page.error);
    CHECK(page.events_shape);
    CHECK(page.events.empty());
    CHECK_FALSE(page.since.has_value());
}

TEST_CASE("longpoll top-level callback body is delivered as one event") {
    auto page = parse_longpoll_response(response(R"({
        "status": "success",
        "result": {"radio_id": 4, "timestamp": "2026-01-01 00:00:00", "play_length": 5,
                   "results": [{"artist": "Old", "title": "Shape", "score": 80}]},
        "timestamp": 1700000000123
    })"));
    CHECK_FALSE(page.error);
    CHECK_FALSE(page.events_shape);
    REQUIRE(page.events.size() == 1);
    CHECK(match_at(page, 0).song.artist == "Old");
    REQUIRE(page.since.has_value());
    CHECK(*page.since == 1700000000123LL);

    auto notif = parse_longpoll_response(response(read_fixture("streams_callback_with_notification.json")));
    REQUIRE(notif.events.size() == 1);
    CHECK(std::holds_alternative<audd::StreamCallbackNotification>(notif.events[0]));
    CHECK_FALSE(notif.since.has_value());
}

TEST_CASE("longpoll unknown, empty and non-JSON bodies yield nothing") {
    for (const char* raw : {"", "not json", "[]", "42", R"({"weird":123})",
                            R"({"status":"error"})", R"({"timeout":"no events before timeout"})",
                            R"({"error":null})"}) {
        CAPTURE(raw);
        auto page = parse_longpoll_response(response(raw));
        CHECK_FALSE(page.error);
        CHECK(page.events.empty());
        CHECK_FALSE(page.since.has_value());
    }
}

TEST_CASE("longpoll backoff starts at 1 second and doubles up to 30 seconds") {
    using std::chrono::milliseconds;
    using audd::internal::kLongpollBackoffMax;
    using audd::internal::kLongpollBackoffMin;
    CHECK(kLongpollBackoffMin == milliseconds(1000));
    CHECK(kLongpollBackoffMax == milliseconds(30000));
    const milliseconds expected[] = {
        milliseconds(1000), milliseconds(2000), milliseconds(4000), milliseconds(8000),
        milliseconds(16000), milliseconds(30000), milliseconds(30000)};
    for (int n = 0; n < 7; ++n) {
        CAPTURE(n);
        CHECK(longpoll_backoff_delay(n, kLongpollBackoffMin, kLongpollBackoffMax) == expected[n]);
    }
    CHECK(longpoll_backoff_delay(64, kLongpollBackoffMin, kLongpollBackoffMax) == milliseconds(30000));
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#include <doctest.h>

#include <audd/callback.hpp>
#include <audd/error.hpp>

TEST_CASE("parse_callback decodes a single-result match") {
    std::string body = R"({
        "result": {
            "radio_id": 7,
            "timestamp": "2026-01-01 00:00:00",
            "play_length": 180,
            "results": [
                {
                    "score": 100,
                    "artist": "Daft Punk",
                    "title": "Get Lucky"
                }
            ]
        }
    })";
    auto ev = audd::parse_callback(body);
    REQUIRE(audd::is_match(ev));
    auto& m = std::get<audd::StreamCallbackMatch>(ev);
    CHECK(m.radio_id == 7);
    CHECK(m.song.artist == "Daft Punk");
    CHECK(m.song.title == "Get Lucky");
    CHECK(m.alternatives.empty());
    CHECK(m.play_length == 180);
}

TEST_CASE("parse_callback splits results into song + alternatives") {
    std::string body = R"({
        "result": {
            "radio_id": 8,
            "results": [
                {"artist": "Variant A", "title": "Original"},
                {"artist": "Variant B", "title": "Cover"}
            ]
        }
    })";
    auto ev = audd::parse_callback(body);
    REQUIRE(audd::is_match(ev));
    auto& m = std::get<audd::StreamCallbackMatch>(ev);
    CHECK(m.song.artist == "Variant A");
    REQUIRE(m.alternatives.size() == 1);
    CHECK(m.alternatives[0].artist == "Variant B");
}

TEST_CASE("parse_callback decodes a notification") {
    std::string body = R"({
        "notification": {
            "radio_id": 7,
            "stream_running": false,
            "notification_code": 100,
            "notification_message": "stream stopped"
        },
        "time": 1700000000
    })";
    auto ev = audd::parse_callback(body);
    REQUIRE(audd::is_notification(ev));
    auto& n = std::get<audd::StreamCallbackNotification>(ev);
    CHECK(n.radio_id == 7);
    CHECK(n.notification_code == 100);
    CHECK(n.notification_message == "stream stopped");
    CHECK(n.time == 1700000000);
    REQUIRE(n.stream_running.has_value());
    CHECK_FALSE(*n.stream_running);
}

TEST_CASE("parse_callback rejects bodies missing both result and notification") {
    std::string body = R"({"foo": "bar"})";
    CHECK_THROWS_AS(audd::parse_callback(body), audd::AuddSerializationError);
}

TEST_CASE("parse_callback rejects empty results array") {
    std::string body = R"({"result": {"radio_id": 1, "results": []}})";
    CHECK_THROWS_AS(audd::parse_callback(body), audd::AuddSerializationError);
}

TEST_CASE("parse_callback rejects invalid json") {
    CHECK_THROWS_AS(audd::parse_callback("{not json"), audd::AuddSerializationError);
}

TEST_CASE("handle_callback alias") {
    std::string body = R"({"notification": {"radio_id": 1, "notification_code": 1, "notification_message": "x"}})";
    auto ev = audd::handle_callback(body);
    REQUIRE(audd::is_notification(ev));
}

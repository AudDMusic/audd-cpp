// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#include <doctest.h>

#include <nlohmann/json.hpp>

#include <audd/recognition.hpp>
#include "internal/json_parse.hpp"

using audd::internal::parse_enterprise_chunk;

TEST_CASE("enterprise chunk parses songs array") {
    auto j = nlohmann::json::parse(R"({
        "offset": "00:00:30",
        "songs": [
            {
                "score": 95,
                "timecode": "00:00:30",
                "artist": "Daft Punk",
                "title": "Get Lucky",
                "album": "RAM",
                "isrc": "USQX91300100",
                "upc": "888837168021",
                "song_link": "https://lis.tn/abcd",
                "start_offset": 0,
                "end_offset": 30
            }
        ]
    })");
    auto c = parse_enterprise_chunk(j);
    CHECK(c.offset == "00:00:30");
    REQUIRE(c.songs.size() == 1);
    CHECK(c.songs[0].artist == "Daft Punk");
    CHECK(c.songs[0].score == 95);
    CHECK(c.songs[0].isrc == "USQX91300100");
    CHECK(c.songs[0].start_offset == 0);
    CHECK(c.songs[0].end_offset == 30);
}

TEST_CASE("enterprise chunk extras capture unknown song-level keys") {
    auto j = nlohmann::json::parse(R"({
        "offset": "00:00:30",
        "songs": [{
            "artist": "x",
            "title": "y",
            "future_field": "future_value"
        }]
    })");
    auto c = parse_enterprise_chunk(j);
    REQUIRE(c.songs.size() == 1);
    REQUIRE(c.songs[0].extras.count("future_field"));
    CHECK(c.songs[0].extras.at("future_field").get<std::string>() == "future_value");
}

TEST_CASE("enterprise song omitting score/isrc/upc/label parses without throwing") {
    // The enterprise endpoint legitimately returns songs with no score and no
    // isrc/upc/label. Parsing must never throw on these absent fields; the
    // corresponding members default (score 0, strings empty).
    auto j = nlohmann::json::parse(R"({
        "offset": "00:01:00",
        "songs": [{
            "timecode": "00:01:00",
            "artist": "Unknown Artist",
            "title": "Untitled",
            "start_offset": 60,
            "end_offset": 90
        }]
    })");
    audd::EnterpriseChunkResult c;
    CHECK_NOTHROW(c = parse_enterprise_chunk(j));
    REQUIRE(c.songs.size() == 1);
    CHECK(c.songs[0].artist == "Unknown Artist");
    CHECK(c.songs[0].title == "Untitled");
    CHECK(c.songs[0].score == 0);       // absent -> default
    CHECK(c.songs[0].isrc.empty());     // absent -> default
    CHECK(c.songs[0].upc.empty());      // absent -> default
    CHECK(c.songs[0].label.empty());    // absent -> default
    CHECK(c.songs[0].start_offset == 60);
    CHECK(c.songs[0].end_offset == 90);
}

TEST_CASE("enterprise song with wrong-typed score does not throw") {
    // Defensive: even if score arrives as an unexpected JSON type, parsing
    // must not throw; the field simply defaults.
    auto j = nlohmann::json::parse(R"({
        "songs": [{
            "score": {"unexpected": "object"},
            "artist": "a",
            "title": "b"
        }]
    })");
    audd::EnterpriseChunkResult c;
    CHECK_NOTHROW(c = parse_enterprise_chunk(j));
    REQUIRE(c.songs.size() == 1);
    CHECK(c.songs[0].score == 0);
    CHECK(c.songs[0].artist == "a");
}

TEST_CASE("enterprise match thumbnail_url") {
    audd::EnterpriseMatch m;
    m.song_link = "https://lis.tn/abcd";
    CHECK(m.thumbnail_url() == "https://lis.tn/abcd?thumb");
    m.song_link = "";
    CHECK(m.thumbnail_url() == "");
}

TEST_CASE("enterprise match streaming_urls returns all providers") {
    audd::EnterpriseMatch m;
    m.song_link = "https://lis.tn/abcd";
    auto urls = m.streaming_urls();
    CHECK(urls.size() == 5); // spotify, apple_music, deezer, napster, youtube
    CHECK(urls[audd::StreamingProvider::Spotify] == "https://lis.tn/abcd?spotify");
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#include <doctest.h>

#include <nlohmann/json.hpp>

#include <audd/recognition.hpp>
#include "internal/json_parse.hpp"

using audd::internal::parse_recognition;

TEST_CASE("recognition parses core fields") {
    auto j = nlohmann::json::parse(R"({
        "timecode": "00:42",
        "artist": "Daft Punk",
        "title": "Get Lucky",
        "album": "Random Access Memories",
        "release_date": "2013-04-19",
        "label": "Columbia",
        "song_link": "https://lis.tn/abcd",
        "isrc": "USQX91300100",
        "upc": "888837168021"
    })");
    auto r = parse_recognition(j);
    CHECK(r.artist == "Daft Punk");
    CHECK(r.title == "Get Lucky");
    CHECK(r.timecode == "00:42");
    CHECK(r.isrc == "USQX91300100");
    CHECK(r.upc == "888837168021");
    CHECK_FALSE(r.audio_id.has_value());
    CHECK(r.is_public_match());
    CHECK_FALSE(r.is_custom_match());
}

TEST_CASE("recognition with audio_id is custom match") {
    auto j = nlohmann::json::parse(R"({
        "audio_id": 42,
        "timecode": "00:00"
    })");
    auto r = parse_recognition(j);
    REQUIRE(r.audio_id.has_value());
    CHECK(*r.audio_id == 42);
    CHECK(r.is_custom_match());
    CHECK_FALSE(r.is_public_match());
}

TEST_CASE("recognition extras capture unknown keys") {
    auto j = nlohmann::json::parse(R"({
        "title": "x",
        "artist": "y",
        "song_length": 180,
        "future_field": "value"
    })");
    auto r = parse_recognition(j);
    REQUIRE(r.extras.count("song_length"));
    REQUIRE(r.extras.count("future_field"));
    CHECK(r.extras.at("song_length").get<int>() == 180);
    CHECK(r.extras.at("future_field").get<std::string>() == "value");
    // Known keys should NOT be in extras.
    CHECK(r.extras.count("title") == 0);
    CHECK(r.extras.count("artist") == 0);
}

TEST_CASE("apple_music block parsed and extras roundtrip") {
    auto j = nlohmann::json::parse(R"({
        "title": "x",
        "apple_music": {
            "artistName": "Daft Punk",
            "url": "https://music.apple.com/abcd",
            "isrc": "ISRC1",
            "previews": [{"url": "https://example.com/preview.m4a"}],
            "genreNames": ["Electronic"]
        }
    })");
    auto r = parse_recognition(j);
    REQUIRE(r.apple_music.has_value());
    CHECK(r.apple_music->artist_name == "Daft Punk");
    CHECK(r.apple_music->url == "https://music.apple.com/abcd");
    CHECK(r.apple_music->isrc == "ISRC1");
    REQUIRE(r.apple_music->extras.count("genreNames"));
    REQUIRE(r.apple_music->extras.count("previews"));
    CHECK(r.preview_url() == "https://example.com/preview.m4a");
}

TEST_CASE("thumbnail_url returns lis.tn redirect") {
    audd::RecognitionResult r;
    r.song_link = "https://lis.tn/abcd";
    CHECK(r.thumbnail_url() == "https://lis.tn/abcd?thumb");
    r.song_link = "https://youtube.com/watch?v=foo";
    CHECK(r.thumbnail_url() == "");
    r.song_link = "";
    CHECK(r.thumbnail_url() == "");
}

TEST_CASE("streaming_url falls back to lis.tn redirect") {
    audd::RecognitionResult r;
    r.song_link = "https://lis.tn/abcd";
    CHECK(r.streaming_url(audd::StreamingProvider::Spotify) == "https://lis.tn/abcd?spotify");
    CHECK(r.streaming_url(audd::StreamingProvider::YouTube) == "https://lis.tn/abcd?youtube");
}

TEST_CASE("streaming_url prefers direct apple_music.url") {
    audd::RecognitionResult r;
    audd::AppleMusicMetadata am;
    am.url = "https://music.apple.com/direct";
    r.apple_music = am;
    r.song_link = "https://lis.tn/abcd";
    CHECK(r.streaming_url(audd::StreamingProvider::AppleMusic) == "https://music.apple.com/direct");
}

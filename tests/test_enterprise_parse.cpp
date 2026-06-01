// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#include <doctest.h>

#include <nlohmann/json.hpp>

#include <audd/recognition.hpp>
#include "internal/json_parse.hpp"

using audd::internal::parse_enterprise_chunk;
using audd::internal::offset_to_seconds;

namespace {
// flatten mirrors the chunk-flattening + offset anchoring performed in
// AudD::recognize_enterprise: it walks the parsed chunks, computes each
// match's absolute file position from the chunk offset plus the
// fragment-relative start_offset/end_offset, and returns the flat vector.
std::vector<audd::EnterpriseMatch> flatten(const nlohmann::json& result) {
    std::vector<audd::EnterpriseMatch> out;
    for (const auto& chunk : result) {
        auto parsed = parse_enterprise_chunk(chunk);
        auto base = offset_to_seconds(parsed.offset);
        for (auto& song : parsed.songs) {
            if (base) {
                song.start_seconds = *base + song.start_offset / 1000.0;
                song.end_seconds   = *base + song.end_offset / 1000.0;
            }
            out.push_back(std::move(song));
        }
    }
    return out;
}
} // namespace

TEST_CASE("offset_to_seconds parses the documented offset shapes") {
    using doctest::Approx;
    CHECK_FALSE(offset_to_seconds("").has_value());
    CHECK(offset_to_seconds("30").value() == Approx(30.0));
    CHECK(offset_to_seconds("00:30").value() == Approx(30.0));
    CHECK(offset_to_seconds("00:01:00").value() == Approx(60.0));
    CHECK(offset_to_seconds("01:02:03").value() == Approx(3723.0));
    CHECK(offset_to_seconds("12.5").value() == Approx(12.5));
    // Unparseable -> nullopt, never throws.
    CHECK_FALSE(offset_to_seconds("not-a-time").has_value());
    CHECK_FALSE(offset_to_seconds("1:2:3:4").has_value());
    CHECK_FALSE(offset_to_seconds("00::30").has_value());
}

TEST_CASE("enterprise flatten anchors start_seconds/end_seconds to the file") {
    using doctest::Approx;
    auto j = nlohmann::json::parse(R"([
        {
            "offset": "00:01:00",
            "songs": [{
                "artist": "Daft Punk",
                "title": "Get Lucky",
                "start_offset": 4200,
                "end_offset": 11800
            }]
        },
        {
            "songs": [{
                "artist": "No Offset",
                "title": "Untitled",
                "start_offset": 500,
                "end_offset": 9000
            }]
        }
    ])");
    auto matches = flatten(j);
    REQUIRE(matches.size() == 2);

    // Chunk offset 60s + 4200ms / 11800ms fragment-relative.
    REQUIRE(matches[0].start_seconds.has_value());
    REQUIRE(matches[0].end_seconds.has_value());
    CHECK(matches[0].start_seconds.value() == Approx(64.2));
    CHECK(matches[0].end_seconds.value() == Approx(71.8));

    // Chunk with no offset -> seconds stay absent; raw offsets still present.
    CHECK_FALSE(matches[1].start_seconds.has_value());
    CHECK_FALSE(matches[1].end_seconds.has_value());
    CHECK(matches[1].start_offset == 500);
    CHECK(matches[1].end_offset == 9000);
}

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

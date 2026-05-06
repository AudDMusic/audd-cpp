// SPDX-License-Identifier: MIT
// Copyright (c) AudD <https://audd.io>
//
// Smoke tests for std::optional and std::variant ergonomics on the public
// types — these are the touch-points users interact with most.

#include <doctest.h>

#include <audd/audd.hpp>

TEST_CASE("RecognitionResult has optional metadata blocks") {
    audd::RecognitionResult r;
    CHECK_FALSE(r.apple_music.has_value());
    CHECK_FALSE(r.spotify.has_value());
    CHECK_FALSE(r.audio_id.has_value());
    r.audio_id = 42;
    REQUIRE(r.audio_id.has_value());
    CHECK(*r.audio_id == 42);
}

TEST_CASE("CallbackEvent variant pattern-matches") {
    audd::StreamCallbackMatch m;
    m.radio_id = 7;
    m.song.artist = "Daft Punk";
    audd::CallbackEvent ev = m;

    REQUIRE(audd::is_match(ev));
    CHECK_FALSE(audd::is_notification(ev));
    auto* mp = audd::match_or_null(ev);
    REQUIRE(mp);
    CHECK(mp->song.artist == "Daft Punk");

    audd::StreamCallbackNotification n;
    n.notification_code = 100;
    ev = n;
    REQUIRE(audd::is_notification(ev));
    CHECK_FALSE(audd::is_match(ev));
}

TEST_CASE("Source variant accepts string/url/path/bytes") {
    audd::Source s1 = std::string("https://example.com/a.mp3");
    audd::Source s2 = audd::SourceUrl{"https://example.com/b.mp3"};
    audd::Source s3 = audd::SourceFilePath{"/tmp/x.mp3"};
    audd::Source s4 = audd::SourceBytes{{}, "x.mp3", "audio/mpeg"};

    CHECK(std::holds_alternative<std::string>(s1));
    CHECK(std::holds_alternative<audd::SourceUrl>(s2));
    CHECK(std::holds_alternative<audd::SourceFilePath>(s3));
    CHECK(std::holds_alternative<audd::SourceBytes>(s4));
}

TEST_CASE("AudD construction with empty token does not throw") {
    audd::AudD client(""); // falls through to env var
    CHECK(true); // construction succeeded
}

TEST_CASE("strict() throws on missing token") {
    // Save and clear AUDD_API_TOKEN if set.
    const char* saved = std::getenv("AUDD_API_TOKEN");
    std::string saved_str = saved ? saved : "";
    unsetenv("AUDD_API_TOKEN");
    CHECK_THROWS_AS(audd::AudD::strict(""), audd::AuddMissingApiTokenError);
    if (!saved_str.empty()) setenv("AUDD_API_TOKEN", saved_str.c_str(), 1);
}

TEST_CASE("version constant is the expected one") {
    CHECK(std::string(audd::version()) == "1.5.2");
    CHECK(std::string(AUDD_VERSION) == "1.5.2");
}

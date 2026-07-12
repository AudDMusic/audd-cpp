// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#include <doctest.h>

#include <nlohmann/json.hpp>

#include <audd/recognition.hpp>
#include "internal/json_parse.hpp"

// The AudD API is loosely typed: a field that is normally one JSON type can
// arrive as another (an int-valued "score":"85", a string-valued
// "artist":123, etc.). The parse layer coerces a wrong-typed *scalar* to the
// expected type when convertible, and degrades to the type's default only when
// it is not. These tests pin the coercion policy at the shared coerce_*
// entry points and through a couple of real model parsers.

using audd::internal::coerce_string;
using audd::internal::coerce_int;
using audd::internal::coerce_double;
using audd::internal::coerce_bool;
using audd::internal::parse_enterprise_chunk;
using audd::internal::parse_recognition;
using audd::internal::parse_stream;

namespace {
nlohmann::json J(const char* s) { return nlohmann::json::parse(s); }
} // namespace

// ---------------------------------------------------------------------------
// coerce_string
// ---------------------------------------------------------------------------
TEST_CASE("coerce_string renders scalars, rejects containers") {
    // Well-typed string passes through.
    CHECK(coerce_string(J(R"("hello")")).value() == "hello");
    // Integer number -> no decimal point.
    CHECK(coerce_string(J("85")).value() == "85");
    // Double -> natural rendering.
    CHECK(coerce_string(J("8.5")).value() == "8.5");
    // Integer-valued double still renders without a trailing ".0".
    CHECK(coerce_string(J("85.0")).value() == "85");
    // Bool -> "true"/"false".
    CHECK(coerce_string(J("true")).value() == "true");
    CHECK(coerce_string(J("false")).value() == "false");
    // Negative + large ints.
    CHECK(coerce_string(J("-42")).value() == "-42");
    // Containers are not convertible.
    CHECK_FALSE(coerce_string(J(R"({"a":1})")).has_value());
    CHECK_FALSE(coerce_string(J("[1,2]")).has_value());
    CHECK_FALSE(coerce_string(J("null")).has_value());
}

// ---------------------------------------------------------------------------
// coerce_int
// ---------------------------------------------------------------------------
TEST_CASE("coerce_int parses numeric strings and truncates doubles") {
    CHECK(coerce_int(J("85")).value() == 85);
    // Numeric string.
    CHECK(coerce_int(J(R"("85")")).value() == 85);
    CHECK(coerce_int(J(R"(" 85 ")")).value() == 85); // trimmed
    CHECK(coerce_int(J(R"("-7")")).value() == -7);
    CHECK(coerce_int(J(R"("+9")")).value() == 9);
    // Double -> truncate toward zero.
    CHECK(coerce_int(J("2.9")).value() == 2);
    CHECK(coerce_int(J("-2.9")).value() == -2);
    CHECK(coerce_int(J("2.5")).value() == 2);
    // Bool -> 0/1.
    CHECK(coerce_int(J("true")).value() == 1);
    CHECK(coerce_int(J("false")).value() == 0);
}

TEST_CASE("coerce_int rejects non-numeric / junk strings") {
    CHECK_FALSE(coerce_int(J(R"("abc")")).has_value());
    CHECK_FALSE(coerce_int(J(R"("85abc")")).has_value()); // trailing junk
    CHECK_FALSE(coerce_int(J(R"("")")).has_value());
    CHECK_FALSE(coerce_int(J(R"("NaN")")).has_value());
    CHECK_FALSE(coerce_int(J(R"("Infinity")")).has_value());
    CHECK_FALSE(coerce_int(J(R"("0x1A")")).has_value()); // hex rejected
    CHECK_FALSE(coerce_int(J(R"("1.5")")).has_value());  // not an integer string
    CHECK_FALSE(coerce_int(J(R"({"a":1})")).has_value());
    CHECK_FALSE(coerce_int(J("[1]")).has_value());
    CHECK_FALSE(coerce_int(J("null")).has_value());
}

// ---------------------------------------------------------------------------
// coerce_double
// ---------------------------------------------------------------------------
TEST_CASE("coerce_double converts ints and parses numeric strings") {
    using doctest::Approx;
    CHECK(coerce_double(J("8.5")).value() == Approx(8.5));
    CHECK(coerce_double(J("42")).value() == Approx(42.0));   // int -> double
    CHECK(coerce_double(J(R"("8.5")")).value() == Approx(8.5));
    CHECK(coerce_double(J(R"(" 8.5 ")")).value() == Approx(8.5)); // trimmed
    CHECK(coerce_double(J(R"("1e3")")).value() == Approx(1000.0)); // exponent ok
    CHECK(coerce_double(J("true")).value() == Approx(1.0));
    CHECK(coerce_double(J("false")).value() == Approx(0.0));
}

TEST_CASE("coerce_double rejects nan/inf/hex/junk strings") {
    CHECK_FALSE(coerce_double(J(R"("NaN")")).has_value());
    CHECK_FALSE(coerce_double(J(R"("Infinity")")).has_value());
    CHECK_FALSE(coerce_double(J(R"("inf")")).has_value());
    CHECK_FALSE(coerce_double(J(R"("0x1A")")).has_value());
    CHECK_FALSE(coerce_double(J(R"("8.5abc")")).has_value());
    CHECK_FALSE(coerce_double(J(R"("")")).has_value());
    CHECK_FALSE(coerce_double(J(R"({"a":1})")).has_value());
    CHECK_FALSE(coerce_double(J("[1]")).has_value());
}

// ---------------------------------------------------------------------------
// coerce_bool — strict whitelist both directions
// ---------------------------------------------------------------------------
TEST_CASE("coerce_bool string whitelist -> true") {
    for (const char* s : {R"("true")", R"("1")", R"("yes")", R"("on")"}) {
        CAPTURE(s);
        CHECK(coerce_bool(J(s)).value() == true);
    }
    // Case-insensitive + trimmed.
    CHECK(coerce_bool(J(R"("TRUE")")).value() == true);
    CHECK(coerce_bool(J(R"(" on ")")).value() == true);
    CHECK(coerce_bool(J(R"("Yes")")).value() == true);
}

TEST_CASE("coerce_bool string whitelist -> false") {
    for (const char* s : {R"("false")", R"("0")", R"("no")", R"("off")", R"("")"}) {
        CAPTURE(s);
        CHECK(coerce_bool(J(s)).value() == false);
    }
    // Case-insensitive + trimmed.
    CHECK(coerce_bool(J(R"("FALSE")")).value() == false);
    CHECK(coerce_bool(J(R"(" No ")")).value() == false);
    CHECK(coerce_bool(J(R"("Off")")).value() == false);
}

TEST_CASE("coerce_bool rejects unrecognized strings (no true-by-default)") {
    CHECK_FALSE(coerce_bool(J(R"("maybe")")).has_value());
    CHECK_FALSE(coerce_bool(J(R"("2")")).has_value());
    CHECK_FALSE(coerce_bool(J(R"("yesno")")).has_value());
}

TEST_CASE("coerce_bool from numbers and booleans") {
    CHECK(coerce_bool(J("true")).value() == true);
    CHECK(coerce_bool(J("false")).value() == false);
    CHECK(coerce_bool(J("1")).value() == true);
    CHECK(coerce_bool(J("0")).value() == false);
    CHECK(coerce_bool(J("2.5")).value() == true);  // !=0
    CHECK(coerce_bool(J("-1")).value() == true);   // !=0
    CHECK(coerce_bool(J("0.0")).value() == false);
    // Containers not convertible.
    CHECK_FALSE(coerce_bool(J(R"({"a":1})")).has_value());
    CHECK_FALSE(coerce_bool(J("[true]")).has_value());
}

// ---------------------------------------------------------------------------
// End-to-end through real model parsers
// ---------------------------------------------------------------------------
TEST_CASE("enterprise score arriving as a numeric string coerces to int") {
    auto j = J(R"({
        "songs": [{"score":"85","artist":"a","title":"b"}]
    })");
    auto c = parse_enterprise_chunk(j);
    REQUIRE(c.songs.size() == 1);
    CHECK(c.songs[0].score == 85);
}

TEST_CASE("enterprise score arriving as a non-numeric string degrades to 0") {
    auto j = J(R"({
        "songs": [{"score":"abc","artist":"a","title":"b"}]
    })");
    auto c = parse_enterprise_chunk(j);
    REQUIRE(c.songs.size() == 1);
    CHECK(c.songs[0].score == 0); // not convertible -> default, never garbage
}

TEST_CASE("enterprise start/end offsets arriving as strings coerce to int") {
    // Offset fields legitimately arrive as numeric strings on some responses.
    auto j = J(R"({
        "offset": "00:01:00",
        "songs": [{
            "artist":"a","title":"b",
            "start_offset":"4200","end_offset":"11800"
        }]
    })");
    auto c = parse_enterprise_chunk(j);
    REQUIRE(c.songs.size() == 1);
    CHECK(c.songs[0].start_offset == 4200);
    CHECK(c.songs[0].end_offset == 11800);
}

TEST_CASE("recognition artist arriving as a number renders to string") {
    auto j = J(R"({"artist":123,"title":"b"})");
    auto r = parse_recognition(j);
    CHECK(r.artist == "123");
    CHECK(r.title == "b");
}

TEST_CASE("recognition audio_id arriving as a numeric string coerces") {
    auto j = J(R"({"audio_id":"4711","timecode":"00:00:00"})");
    auto r = parse_recognition(j);
    REQUIRE(r.audio_id.has_value());
    CHECK(r.audio_id.value() == 4711);
}

TEST_CASE("recognition audio_id arriving as a double truncates toward zero") {
    auto j = J(R"({"audio_id":42.9})");
    auto r = parse_recognition(j);
    REQUIRE(r.audio_id.has_value());
    CHECK(r.audio_id.value() == 42);
}

TEST_CASE("recognition audio_id that is not convertible stays absent") {
    auto j = J(R"({"audio_id":"not-a-number"})");
    auto r = parse_recognition(j);
    CHECK_FALSE(r.audio_id.has_value());
}

TEST_CASE("stream_running arriving as a string coerces via whitelist") {
    auto on  = parse_stream(J(R"({"radio_id":1,"stream_running":"true"})"));
    CHECK(on.stream_running == true);
    auto off = parse_stream(J(R"({"radio_id":1,"stream_running":"no"})"));
    CHECK(off.stream_running == false);
    // Numeric radio_id arriving as a string coerces too.
    auto s = parse_stream(J(R"({"radio_id":"7","stream_running":true})"));
    CHECK(s.radio_id == 7);
}

// ---------------------------------------------------------------------------
// Wrong-shaped containers keep degrading; well-typed fast path is unaffected.
// ---------------------------------------------------------------------------
TEST_CASE("wrong-shaped container still degrades to default") {
    auto j = J(R"({
        "songs": [{"score":{"unexpected":"object"},"artist":"a","title":"b"}]
    })");
    auto c = parse_enterprise_chunk(j);
    REQUIRE(c.songs.size() == 1);
    CHECK(c.songs[0].score == 0);
    // Array where a scalar string was expected -> empty string.
    auto r = parse_recognition(J(R"({"artist":[1,2,3],"title":"ok"})"));
    CHECK(r.artist.empty());
    CHECK(r.title == "ok");
}

TEST_CASE("well-typed response takes the fast path unchanged") {
    auto j = J(R"({
        "offset":"00:00:30",
        "songs":[{"score":95,"artist":"Daft Punk","title":"Get Lucky",
                  "start_offset":0,"end_offset":30}]
    })");
    auto c = parse_enterprise_chunk(j);
    REQUIRE(c.songs.size() == 1);
    CHECK(c.songs[0].score == 95);
    CHECK(c.songs[0].artist == "Daft Punk");
    CHECK(c.songs[0].start_offset == 0);
    CHECK(c.songs[0].end_offset == 30);
}

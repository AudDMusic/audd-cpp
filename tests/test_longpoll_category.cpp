// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#include <doctest.h>
#include <string>
#include <type_traits>

#include <audd/client.hpp>
#include <audd/longpoll.hpp>
#include <audd/streams.hpp>
#include "internal/md5.hpp"

TEST_CASE("md5_hex matches known vectors") {
    // Standard MD5 test vectors.
    CHECK(audd::internal::md5_hex("") == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(audd::internal::md5_hex("a") == "0cc175b9c0f1b6a831c399e269772661");
    CHECK(audd::internal::md5_hex("abc") == "900150983cd24fb0d6963f7d28e17f72");
    CHECK(audd::internal::md5_hex("The quick brown fox jumps over the lazy dog")
          == "9e107d9d372bb6826bd81d3542a419d6");
}

TEST_CASE("derive_longpoll_category formula: md5(md5(token) + radio_id)[:9]") {
    // Reference computation: MD5("test") = "098f6bcd4621d373cade4e832627b4f6"
    // MD5("098f6bcd4621d373cade4e832627b4f6" + "1") truncated to 9 chars.
    std::string inner = audd::internal::md5_hex("test");
    std::string outer = audd::internal::md5_hex(inner + "1");
    std::string expected = outer.substr(0, 9);
    CHECK(audd::derive_longpoll_category("test", 1) == expected);
    CHECK(audd::derive_longpoll_category("test", 1).size() == 9);
}

TEST_CASE("derive_longpoll_category varies by radio_id and token") {
    auto a = audd::derive_longpoll_category("test", 1);
    auto b = audd::derive_longpoll_category("test", 2);
    auto c = audd::derive_longpoll_category("other", 1);
    CHECK(a != b);
    CHECK(a != c);
    CHECK(b != c);
}

TEST_CASE("StreamsClient::derive_longpoll_category matches free function") {
    // Wire-identity equivalence guard: the new longpoll(int radio_id, ...)
    // overload derives its category via this member function, then delegates
    // to longpoll(const std::string&, ...). If the member result matches the
    // free function called with the parent's token + radio_id, the new
    // overload sends the exact same `category` query parameter as a manual
    // two-step caller would.
    audd::AudD client("test");
    for (int radio_id : {1, 2, 17, 42, 1000}) {
        std::string via_member = client.streams().derive_longpoll_category(radio_id);
        std::string via_free   = audd::derive_longpoll_category("test", radio_id);
        CHECK(via_member == via_free);
        CHECK(via_member.size() == 9);
    }
}

TEST_CASE("StreamsClient::longpoll has both string and int overloads") {
    // Compile-time check that both overloads exist and resolve. We don't
    // invoke them here (each spins up an HTTP worker thread); we just
    // verify the call expressions are well-formed via std::is_invocable.
    using S = audd::StreamsClient;
    static_assert(std::is_invocable_r_v<audd::LongpollPoll, decltype(static_cast<audd::LongpollPoll(S::*)(const std::string&, const audd::LongpollOptions&)>(&S::longpoll)), S&, const std::string&, const audd::LongpollOptions&>);
    static_assert(std::is_invocable_r_v<audd::LongpollPoll, decltype(static_cast<audd::LongpollPoll(S::*)(int, const audd::LongpollOptions&)>(&S::longpoll)), S&, int, const audd::LongpollOptions&>);
    CHECK(true); // runtime witness for ctest reporting
}

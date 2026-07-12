// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#include <doctest.h>

#include <chrono>
#include <string>
#include <thread>

#include <httplib.h>

#include <audd/error.hpp>
#include "internal/http_client.hpp"

using audd::internal::FileField;
using audd::internal::FormFields;
using audd::internal::HttpClient;

namespace {

// Spins up a local httplib server on an ephemeral port and returns it plus the
// chosen port. Caller stops it.
struct LocalServer {
    httplib::Server srv;
    std::thread     thread;
    int             port = 0;

    ~LocalServer() {
        srv.stop();
        if (thread.joinable()) thread.join();
    }
    void start() {
        port = srv.bind_to_any_port("127.0.0.1");
        thread = std::thread([this] { srv.listen_after_bind(); });
        // Wait for the listener to come up.
        for (int i = 0; i < 200 && !srv.is_running(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    std::string base() const { return "http://127.0.0.1:" + std::to_string(port); }
};

} // namespace

TEST_CASE("post_form_connect_error_no_crash") {
    // 127.0.0.1:9 (discard) refuses connections. A multipart upload that
    // fails to connect must surface a clean AudDConnectionError, never a
    // double-free / use-after-free crash. This exercises the real cleanup
    // path in HttpClient::post_form under ASan.
    HttpClient client("0123456789abcdef0123456789abcdef",
                      std::chrono::milliseconds(2000));
    FormFields f;
    f.data["k"] = "v";
    FileField file;
    file.name = "clip.wav";
    file.bytes = {0x00, 0x01, 0x02, 0x03};
    f.file = file;

    CHECK_THROWS_AS(client.post_form("http://127.0.0.1:9/", f),
                    audd::AudDConnectionError);
}

TEST_CASE("post_form_urlencoded_connect_error_no_crash") {
    // Same, for the non-multipart (urlencoded) path: the header slist must be
    // released exactly once when the connection fails.
    HttpClient client("0123456789abcdef0123456789abcdef",
                      std::chrono::milliseconds(2000));
    FormFields f;
    f.data["url"] = "https://example.com/a.mp3";

    CHECK_THROWS_AS(client.post_form("http://127.0.0.1:9/", f),
                    audd::AudDConnectionError);
}

TEST_CASE("get_connect_error_no_crash") {
    HttpClient client("0123456789abcdef0123456789abcdef",
                      std::chrono::milliseconds(2000));
    CHECK_THROWS_AS(client.get("http://127.0.0.1:9/", {{"a", "b"}}),
                    audd::AudDConnectionError);
}

TEST_CASE("post_form_multipart_success_roundtrip") {
    LocalServer server;
    server.srv.Post("/echo", [](const httplib::Request& req, httplib::Response& res) {
        // Confirm the file part and a data field arrived.
        bool has_file = req.has_file("file");
        bool has_k    = req.has_file("k");
        res.set_header("X-Request-Id", "req-123");
        res.set_content(
            std::string("{\"got_file\":") + (has_file ? "true" : "false") +
            ",\"got_k\":" + (has_k ? "true" : "false") + "}",
            "application/json");
    });
    server.start();
    REQUIRE(server.port != 0);

    HttpClient client("", std::chrono::milliseconds(5000));
    FormFields f;
    f.data["k"] = "v";
    FileField file;
    file.name = "clip.wav";
    file.content_type = "audio/wav";
    file.bytes = {0x10, 0x20, 0x30};
    f.file = file;

    auto resp = client.post_form(server.base() + "/echo", f);
    CHECK(resp.http_status == 200);
    CHECK(resp.request_id == "req-123");
    REQUIRE(resp.json_body.is_object());
    CHECK(resp.json_body.value("got_file", false) == true);
    CHECK(resp.json_body.value("got_k", false) == true);
}

TEST_CASE("get_success_roundtrip") {
    LocalServer server;
    server.srv.Get("/streams", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"success\",\"result\":[]}", "application/json");
    });
    server.start();
    REQUIRE(server.port != 0);

    HttpClient client("", std::chrono::milliseconds(5000));
    auto resp = client.get(server.base() + "/streams", {{"category", "abc"}});
    CHECK(resp.http_status == 200);
    REQUIRE(resp.json_body.is_object());
    CHECK(resp.json_body.value("status", std::string{}) == "success");
}

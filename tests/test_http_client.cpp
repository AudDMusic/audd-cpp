// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#include <doctest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <curl/curl.h>

#include <audd/error.hpp>
#include "internal/http_client.hpp"
#include "internal/retry.hpp"
#include "test_support.hpp"

using audd::internal::failure_is_pre_upload;
using audd::internal::FileField;
using audd::internal::FormFields;
using audd::internal::GetOptions;
using audd::internal::HttpClient;
using audd::internal::retry_on_connection_error;
using audd::internal::RetryGate;
using audd_test::LocalServer;

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

TEST_CASE("failure_is_pre_upload: classification of CURLE codes") {
    // Codes that can only occur before the transfer starts are pre-upload
    // regardless of the probes.
    CHECK(failure_is_pre_upload(CURLE_COULDNT_RESOLVE_HOST, 0, 0));
    CHECK(failure_is_pre_upload(CURLE_COULDNT_RESOLVE_PROXY, 0, 0));
    CHECK(failure_is_pre_upload(CURLE_COULDNT_CONNECT, 0, 0));
    CHECK(failure_is_pre_upload(CURLE_SSL_CONNECT_ERROR, 0, 0));
    CHECK(failure_is_pre_upload(CURLE_PEER_FAILED_VERIFICATION, 0, 0));
    CHECK(failure_is_pre_upload(CURLE_URL_MALFORMAT, 0, 0));

    // A timeout is ambiguous: pre-upload only when the probes prove no body
    // byte was sent and no status line came back.
    CHECK(failure_is_pre_upload(CURLE_OPERATION_TIMEDOUT, 0, 0));
    // Timed out AFTER the body was uploaded — the server may already have
    // done (and billed) the metered work. Must NOT classify as pre-upload.
    CHECK_FALSE(failure_is_pre_upload(CURLE_OPERATION_TIMEDOUT, 48213, 0));
    // A status line arrived, so the request reached the server's app layer.
    CHECK_FALSE(failure_is_pre_upload(CURLE_OPERATION_TIMEDOUT, 48213, 200));

    // Other ambiguous codes follow the same probe rule.
    CHECK(failure_is_pre_upload(CURLE_SEND_ERROR, 0, 0));
    CHECK_FALSE(failure_is_pre_upload(CURLE_SEND_ERROR, 512, 0));
    CHECK_FALSE(failure_is_pre_upload(CURLE_RECV_ERROR, 48213, 0));
    CHECK_FALSE(failure_is_pre_upload(CURLE_PARTIAL_FILE, 48213, 200));
    CHECK_FALSE(failure_is_pre_upload(CURLE_GOT_NOTHING, 48213, 0));
}

TEST_CASE("connect_refused_is_pre_upload_and_retried") {
    // 127.0.0.1:9 refuses connections: the body is never sent, so the thrown
    // error must carry failed_before_upload()==true and the PreUploadOnly
    // gate (recognize / mutating stream ops) must keep retrying it.
    HttpClient client("0123456789abcdef0123456789abcdef",
                      std::chrono::milliseconds(2000));
    FormFields f;
    f.data["k"] = "v";
    FileField file;
    file.name = "clip.wav";
    file.bytes = {0x00, 0x01, 0x02, 0x03};
    f.file = file;

    int attempts = 0;
    bool caught = false;
    try {
        retry_on_connection_error(
            /*max_attempts=*/3,
            std::chrono::milliseconds(0),
            [&]() {
                ++attempts;
                return client.post_form("http://127.0.0.1:9/", f);
            },
            RetryGate::PreUploadOnly);
    } catch (const audd::AudDConnectionError& e) {
        caught = true;
        CHECK(e.failed_before_upload());
    }
    CHECK(caught);
    CHECK(attempts == 3);
}

TEST_CASE("timeout_after_upload_is_post_upload_and_not_retried") {
    // The server fully receives the multipart body, then never answers within
    // the client timeout. curl reports CURLE_OPERATION_TIMEDOUT with body
    // bytes already uploaded — the server may have done the metered work, so
    // the error must carry failed_before_upload()==false and the
    // PreUploadOnly gate must NOT re-send the upload.
    LocalServer server;
    std::atomic<int> hits{0};
    server.srv.Post("/slow", [&hits](const httplib::Request&, httplib::Response& res) {
        ++hits; // body already parsed by httplib when the handler runs
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        res.set_content("{\"status\":\"success\"}", "application/json");
    });
    server.start();
    REQUIRE(server.port != 0);

    HttpClient client("", std::chrono::milliseconds(500));
    FormFields f;
    f.data["k"] = "v";
    FileField file;
    file.name = "clip.wav";
    file.content_type = "audio/wav";
    file.bytes = std::vector<std::uint8_t>(4096, 0x5a);
    f.file = file;

    int attempts = 0;
    bool caught = false;
    try {
        retry_on_connection_error(
            /*max_attempts=*/5,
            std::chrono::milliseconds(0),
            [&]() {
                ++attempts;
                return client.post_form(server.base() + "/slow", f);
            },
            RetryGate::PreUploadOnly);
    } catch (const audd::AudDConnectionError& e) {
        caught = true;
        CHECK_FALSE(e.failed_before_upload());
    }
    CHECK(caught);
    CHECK(attempts == 1);
    CHECK(hits.load() == 1); // the server saw the upload exactly once
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

TEST_CASE("get adds api_token unless send_token is false") {
    std::mutex mu;
    std::vector<std::string> targets;
    LocalServer server;
    server.srv.Get("/q", [&](const httplib::Request& req, httplib::Response& res) {
        {
            std::lock_guard<std::mutex> lk(mu);
            targets.push_back(req.target);
        }
        res.set_content("{}", "application/json");
    });
    server.start();
    REQUIRE(server.port != 0);

    HttpClient client("secret-token-123", std::chrono::milliseconds(5000));
    client.get(server.base() + "/q", {{"category", "abc"}});
    GetOptions no_token;
    no_token.send_token = false;
    client.get(server.base() + "/q", {{"category", "abc"}}, no_token);

    std::lock_guard<std::mutex> lk(mu);
    REQUIRE(targets.size() == 2);
    CHECK(targets[0].find("api_token=secret-token-123") != std::string::npos);
    CHECK(targets[1].find("api_token") == std::string::npos);
    CHECK(targets[1].find("secret-token-123") == std::string::npos);
    CHECK(targets[1].find("category=abc") != std::string::npos);
}

TEST_CASE("get stops a waiting request once the abort flag is set") {
    std::atomic<bool> release{false};
    LocalServer server;
    server.srv.Get("/hold", [&](const httplib::Request&, httplib::Response& res) {
        audd_test::wait_until([&] { return release.load(); }, std::chrono::seconds(10));
        res.set_content("{}", "application/json");
    });
    server.start();
    REQUIRE(server.port != 0);

    HttpClient client("", std::chrono::milliseconds(20000));
    std::atomic<bool> abort{false};
    GetOptions opts;
    opts.abort = &abort;
    std::thread setter([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        abort.store(true);
    });
    const auto start = std::chrono::steady_clock::now();
    CHECK_THROWS_AS(client.get(server.base() + "/hold", {}, opts), audd::AudDConnectionError);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    setter.join();
    release.store(true);
    CHECK(elapsed < std::chrono::seconds(3));
}

TEST_CASE("get honours a per-request timeout") {
    std::atomic<bool> release{false};
    LocalServer server;
    server.srv.Get("/hold", [&](const httplib::Request&, httplib::Response& res) {
        audd_test::wait_until([&] { return release.load(); }, std::chrono::seconds(10));
        res.set_content("{}", "application/json");
    });
    server.start();
    REQUIRE(server.port != 0);

    HttpClient client("", std::chrono::milliseconds(20000));
    GetOptions opts;
    opts.timeout = std::chrono::milliseconds(300);
    const auto start = std::chrono::steady_clock::now();
    CHECK_THROWS_AS(client.get(server.base() + "/hold", {}, opts), audd::AudDConnectionError);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    release.store(true);
    CHECK(elapsed < std::chrono::seconds(3));
}

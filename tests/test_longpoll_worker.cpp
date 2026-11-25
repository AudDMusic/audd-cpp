// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)
//
// Drives the longpoll worker against a local server that plays back the
// shared fixtures, and checks what it delivers and what it sends next.

#include <doctest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include <audd/error.hpp>
#include <audd/longpoll.hpp>
#include "internal/http_client.hpp"
#include "internal/longpoll_internal.hpp"
#include "test_support.hpp"

using namespace std::chrono_literals;
using audd::internal::HttpClient;
using audd::internal::LongpollWorkerConfig;
using audd::internal::start_longpoll_worker;
using audd_test::read_fixture;
using audd_test::wait_until;

namespace {

constexpr const char* kToken = "secret-token-123";
constexpr const char* kHeldBody = R"({"timeout":"no events before timeout","timestamp":1})";

// LongpollServer answers GET /longpoll/ with respond(n, res) for the n-th
// request (from 0) and records every request.
struct LongpollServer {
    std::mutex                                      mu;
    std::vector<std::string>                        targets;
    std::vector<std::map<std::string, std::string>> params;
    std::atomic<bool>                               release{false};
    std::function<void(std::size_t, httplib::Response&)> respond;
    audd_test::LocalServer                          server; // destroyed first

    ~LongpollServer() { release.store(true); }

    void start() {
        server.srv.Get("/longpoll/", [this](const httplib::Request& req, httplib::Response& res) {
            std::size_t n = 0;
            {
                std::lock_guard<std::mutex> lk(mu);
                n = targets.size();
                targets.push_back(req.target);
                params.emplace_back(req.params.begin(), req.params.end());
            }
            respond(n, res);
        });
        server.start();
        REQUIRE(server.port != 0);
    }

    // hold keeps the request open like a server waiting for events, until
    // the test releases it.
    void hold(httplib::Response& res) {
        wait_until([&] { return release.load(); }, 5s);
        res.set_content(kHeldBody, "application/json");
    }

    std::size_t count() {
        std::lock_guard<std::mutex> lk(mu);
        return targets.size();
    }
    std::map<std::string, std::string> params_at(std::size_t i) {
        std::lock_guard<std::mutex> lk(mu);
        REQUIRE(i < params.size());
        return params[i];
    }
    std::vector<std::string> all_targets() {
        std::lock_guard<std::mutex> lk(mu);
        return targets;
    }
    std::string url() const { return server.base() + "/longpoll/"; }
};

LongpollWorkerConfig make_config(HttpClient& http, const std::string& url) {
    LongpollWorkerConfig cfg;
    cfg.http            = &http;
    cfg.url             = url;
    cfg.category        = "92b1cc7f0";
    cfg.timeout_seconds = 50;
    cfg.request_timeout = 10s;
    cfg.backoff_min     = 50ms;
    cfg.backoff_max     = 200ms;
    return cfg;
}

void json_reply(httplib::Response& res, const std::string& body, int status = 200) {
    res.status = status;
    res.set_content(body, "application/json");
}

// stop closes the poll and lets a held request answer, so the worker exits
// at once.
void stop(audd::LongpollPoll& poll, LongpollServer& srv) {
    poll.close();
    srv.release.store(true);
}

} // namespace

TEST_CASE("longpoll worker: events fixture delivers both songs, then resumes after the newest event") {
    LongpollServer srv;
    srv.respond = [&](std::size_t n, httplib::Response& res) {
        if (n == 0) json_reply(res, read_fixture("longpoll_events.json"));
        else srv.hold(res);
    };
    srv.start();
    HttpClient http(kToken, 5s);
    auto poll = start_longpoll_worker(make_config(http, srv.url()));

    auto first = poll.next_match();
    REQUIRE(first.has_value());
    CHECK(first->radio_id == 7);
    CHECK(first->song.artist == "Alan Walker, A$AP Rocky");
    CHECK(first->song.title == "Live Fast (PUBGM)");
    auto second = poll.next_match();
    REQUIRE(second.has_value());
    CHECK(second->radio_id == 7);
    CHECK(second->song.artist == "Tears For Fears");
    CHECK(second->song.title == "Everybody Wants To Rule The World");

    REQUIRE(wait_until([&] { return srv.count() >= 2; }, 3s));
    auto p0 = srv.params_at(0);
    CHECK(p0["category"] == "92b1cc7f0");
    CHECK(p0["timeout"] == "50");
    CHECK(p0.count("since_time") == 0);
    CHECK(srv.params_at(1)["since_time"] == "1795547548112");

    stop(poll, srv);
    for (const auto& t : srv.all_targets()) {
        CAPTURE(t);
        CHECK(t.find("api_token") == std::string::npos);
        CHECK(t.find(kToken) == std::string::npos);
    }
}

TEST_CASE("longpoll worker: timeout fixture delivers nothing, next request carries its timestamp") {
    LongpollServer srv;
    srv.respond = [&](std::size_t n, httplib::Response& res) {
        if (n == 0) json_reply(res, read_fixture("longpoll_no_events.json"));
        else srv.hold(res);
    };
    srv.start();
    HttpClient http(kToken, 5s);
    auto poll = start_longpoll_worker(make_config(http, srv.url()));

    REQUIRE(wait_until([&] { return srv.count() >= 2; }, 3s));
    CHECK(srv.params_at(1)["since_time"] == "1777901270049");
    stop(poll, srv);
    CHECK_FALSE(poll.next_match().has_value());
    CHECK_FALSE(poll.next_notification().has_value());
    CHECK_FALSE(poll.next_error().has_value());
}

TEST_CASE("longpoll worker: error fixture reaches the error path after exactly one request") {
    LongpollServer srv;
    srv.respond = [&](std::size_t, httplib::Response& res) {
        json_reply(res, read_fixture("longpoll_error.json"));
    };
    srv.start();
    HttpClient http(kToken, 5s);
    auto poll = start_longpoll_worker(make_config(http, srv.url()));

    int errors = 0;
    std::string message;
    audd::ErrorCategory category = audd::ErrorCategory::Server;
    poll.run({}, {}, [&](std::exception_ptr ep) {
        ++errors;
        try {
            std::rethrow_exception(ep);
        } catch (const audd::AudDInvalidRequestError& e) {
            message = e.message;
            category = e.category();
        } catch (...) {
        }
    });
    CHECK(errors == 1);
    CHECK(message == "Invalid timeout arg.  Must be 1-120.");
    CHECK(category == audd::ErrorCategory::InvalidRequest);
    std::this_thread::sleep_for(300ms); // room for a retry that must not happen
    CHECK(srv.count() == 1);
}

TEST_CASE("longpoll worker: a top-level callback body still delivers and advances since_time") {
    LongpollServer srv;
    srv.respond = [&](std::size_t n, httplib::Response& res) {
        if (n == 0) {
            json_reply(res, R"({"status":"success","timestamp":1700000000123,
                "result":{"radio_id":4,"timestamp":"2026-01-01 00:00:00","play_length":5,
                          "results":[{"artist":"Old","title":"Shape","score":80}]}})");
        } else {
            srv.hold(res);
        }
    };
    srv.start();
    HttpClient http(kToken, 5s);
    auto poll = start_longpoll_worker(make_config(http, srv.url()));

    auto m = poll.next_match();
    REQUIRE(m.has_value());
    CHECK(m->radio_id == 4);
    CHECK(m->song.artist == "Old");
    REQUIRE(wait_until([&] { return srv.count() >= 2; }, 3s));
    CHECK(srv.params_at(1)["since_time"] == "1700000000123");
    stop(poll, srv);
}

TEST_CASE("longpoll worker: bodies with nothing new don't spin") {
    for (const char* body : {"<html>bad gateway</html>", "", R"({"events":[]})",
                             R"({"weird":1})", R"({"timeout":"no events before timeout"})"}) {
        CAPTURE(body);
        LongpollServer srv;
        srv.respond = [&](std::size_t, httplib::Response& res) {
            res.set_content(body, "application/json");
        };
        srv.start();
        HttpClient http(kToken, 5s);
        auto poll = start_longpoll_worker(make_config(http, srv.url()));
        // 50 ms doubling to 200 ms: requests at about 0, 50, 150, 350 ms.
        std::this_thread::sleep_for(500ms);
        const auto n = srv.count();
        poll.close();
        CHECK(n >= 2);
        CHECK(n <= 6);
        CHECK_FALSE(poll.next_error().has_value());
    }
}

TEST_CASE("longpoll worker: with the default backoff a junk body is re-requested after about a second") {
    LongpollServer srv;
    srv.respond = [&](std::size_t, httplib::Response& res) {
        res.set_content("not json", "text/plain");
    };
    srv.start();
    HttpClient http(kToken, 5s);
    auto cfg = make_config(http, srv.url());
    cfg.backoff_min = audd::internal::kLongpollBackoffMin;
    cfg.backoff_max = audd::internal::kLongpollBackoffMax;
    auto poll = start_longpoll_worker(cfg);
    std::this_thread::sleep_for(1500ms); // requests at about 0 s and 1 s
    const auto n = srv.count();
    poll.close();
    CHECK(n >= 1);
    CHECK(n <= 2);
}

TEST_CASE("longpoll worker: HTTP 5xx and 429 are retried, reported, and don't end the poll") {
    LongpollServer srv;
    srv.respond = [&](std::size_t n, httplib::Response& res) {
        if (n == 0) json_reply(res, "Bad Gateway", 502);
        else if (n == 1) json_reply(res, R"({"status":"error"})", 429);
        else if (n == 2) json_reply(res, read_fixture("longpoll_events.json"));
        else srv.hold(res);
    };
    srv.start();
    HttpClient http(kToken, 5s);
    auto cfg = make_config(http, srv.url());
    std::mutex mu;
    std::vector<int> statuses;
    cfg.on_transient_error = [&](std::exception_ptr ep) {
        try {
            std::rethrow_exception(ep);
        } catch (const audd::AudDApiError& e) {
            std::lock_guard<std::mutex> lk(mu);
            statuses.push_back(e.http_status);
        } catch (...) {
            std::lock_guard<std::mutex> lk(mu);
            statuses.push_back(-1);
        }
    };
    auto poll = start_longpoll_worker(cfg);

    auto first = poll.next_match();
    REQUIRE(first.has_value());
    CHECK(first->song.title == "Live Fast (PUBGM)");
    auto second = poll.next_match();
    REQUIRE(second.has_value());
    CHECK(second->song.title == "Everybody Wants To Rule The World");
    stop(poll, srv);
    CHECK_FALSE(poll.next_error().has_value());
    std::lock_guard<std::mutex> lk(mu);
    CHECK(statuses == std::vector<int>{502, 429});
}

TEST_CASE("longpoll worker: network failures are retried with backoff and reported") {
    HttpClient http(kToken, 2s);
    auto cfg = make_config(http, "http://127.0.0.1:9/longpoll/"); // connection refused
    std::atomic<int> failures{0};
    std::atomic<bool> connection_errors{true};
    cfg.on_transient_error = [&](std::exception_ptr ep) {
        try {
            std::rethrow_exception(ep);
        } catch (const audd::AudDConnectionError&) {
        } catch (...) {
            connection_errors.store(false);
        }
        ++failures;
    };
    auto poll = start_longpoll_worker(cfg);
    std::this_thread::sleep_for(500ms);
    poll.close();
    CHECK(failures.load() >= 2);
    CHECK(failures.load() <= 6);
    CHECK(connection_errors.load());
    CHECK_FALSE(poll.next_error().has_value());
}

TEST_CASE("longpoll worker: other HTTP 4xx ends the poll with an AudDApiError") {
    LongpollServer srv;
    srv.respond = [&](std::size_t, httplib::Response& res) { json_reply(res, "{}", 404); };
    srv.start();
    HttpClient http(kToken, 5s);
    auto poll = start_longpoll_worker(make_config(http, srv.url()));
    auto err = poll.next_error();
    REQUIRE(err.has_value());
    try {
        std::rethrow_exception(*err);
    } catch (const audd::AudDApiError& e) {
        CHECK(e.http_status == 404);
    } catch (...) {
        FAIL("expected AudDApiError");
    }
    CHECK_FALSE(poll.next_match().has_value());
    CHECK(srv.count() == 1);
}

TEST_CASE("longpoll worker: close stops a request the server is holding") {
    LongpollServer srv;
    srv.respond = [&](std::size_t, httplib::Response& res) { srv.hold(res); };
    srv.start();
    HttpClient http(kToken, 5s);
    auto cfg = make_config(http, srv.url());
    cfg.request_timeout = 30s;
    auto poll = start_longpoll_worker(cfg);
    REQUIRE(wait_until([&] { return srv.count() == 1; }, 3s));
    std::this_thread::sleep_for(100ms);

    const auto start = std::chrono::steady_clock::now();
    poll = audd::LongpollPoll(); // closes and joins the worker
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK(elapsed < 3s);
    CHECK(srv.count() == 1);
}

TEST_CASE("longpoll worker: close interrupts the pause between requests") {
    LongpollServer srv;
    srv.respond = [&](std::size_t, httplib::Response& res) { res.set_content("junk", "text/plain"); };
    srv.start();
    HttpClient http(kToken, 5s);
    auto cfg = make_config(http, srv.url());
    cfg.backoff_min = 20s;
    cfg.backoff_max = 20s;
    auto poll = start_longpoll_worker(cfg);
    REQUIRE(wait_until([&] { return srv.count() == 1; }, 3s));
    std::this_thread::sleep_for(100ms);

    const auto start = std::chrono::steady_clock::now();
    poll = audd::LongpollPoll();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK(elapsed < 1s);
}

TEST_CASE("longpoll run(): callbacks run on the calling thread in arrival order, error last") {
    const auto fixture = nlohmann::json::parse(read_fixture("longpoll_events.json"));
    nlohmann::json body;
    body["events"] = nlohmann::json::array({
        fixture["events"][0],
        {{"timestamp", 1795547500000LL},
         {"data", nlohmann::json::parse(read_fixture("streams_callback_with_notification.json"))}},
        fixture["events"][1],
    });
    LongpollServer srv;
    srv.respond = [&](std::size_t n, httplib::Response& res) {
        if (n == 0) json_reply(res, body.dump());
        else json_reply(res, "{}", 404);
    };
    srv.start();
    HttpClient http(kToken, 5s);
    auto poll = start_longpoll_worker(make_config(http, srv.url()));

    const auto caller = std::this_thread::get_id();
    std::vector<std::string> seen;
    bool all_on_caller = true;
    poll.run(
        [&](audd::StreamCallbackMatch m) {
            all_on_caller = all_on_caller && std::this_thread::get_id() == caller;
            seen.push_back("match:" + m.song.artist);
        },
        [&](audd::StreamCallbackNotification n) {
            all_on_caller = all_on_caller && std::this_thread::get_id() == caller;
            seen.push_back("notification:" + std::to_string(n.notification_code));
        },
        [&](std::exception_ptr) {
            all_on_caller = all_on_caller && std::this_thread::get_id() == caller;
            seen.push_back("error");
        });
    CHECK(all_on_caller);
    CHECK(seen == std::vector<std::string>{
        "match:Alan Walker, A$AP Rocky", "notification:650", "match:Tears For Fears", "error"});
}

TEST_CASE("longpoll run(): an exception from a callback propagates out of run()") {
    LongpollServer srv;
    srv.respond = [&](std::size_t n, httplib::Response& res) {
        if (n == 0) json_reply(res, read_fixture("longpoll_events.json"));
        else srv.hold(res);
    };
    srv.start();
    HttpClient http(kToken, 5s);
    auto poll = start_longpoll_worker(make_config(http, srv.url()));
    CHECK_THROWS_AS(poll.run([](audd::StreamCallbackMatch) { throw std::runtime_error("boom"); }),
                    std::runtime_error);
    // The poll stays open: the second song is still there.
    auto next = poll.next_match();
    REQUIRE(next.has_value());
    CHECK(next->song.artist == "Tears For Fears");
    stop(poll, srv);
}

TEST_CASE("longpoll run(): close() from a callback ends run()") {
    LongpollServer srv;
    srv.respond = [&](std::size_t n, httplib::Response& res) {
        if (n == 0) json_reply(res, read_fixture("longpoll_events.json"));
        else srv.hold(res);
    };
    srv.start();
    HttpClient http(kToken, 5s);
    auto poll = start_longpoll_worker(make_config(http, srv.url()));
    int matches = 0;
    poll.run([&](audd::StreamCallbackMatch) {
        ++matches;
        poll.close();
        srv.release.store(true);
    });
    // Values queued before close() are still delivered.
    CHECK(matches >= 1);
    CHECK(matches <= 2);
}

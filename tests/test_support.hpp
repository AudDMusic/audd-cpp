// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)
//
// Shared helpers for the test suite: fixture loading and a local HTTP server.

#ifndef AUDD_TEST_SUPPORT_HPP
#define AUDD_TEST_SUPPORT_HPP

#include <chrono>
#include <fstream>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#include <httplib.h>

namespace audd_test {

// read_fixture returns the text of tests/fixtures/<name>.
inline std::string read_fixture(const std::string& name) {
    const std::string path = std::string(AUDD_TEST_FIXTURES_DIR) + "/" + name;
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("missing fixture: " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// LocalServer is an httplib server on an ephemeral 127.0.0.1 port. Register
// handlers on `srv`, then call start(). Stopped and joined on destruction.
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

// wait_until polls `done` every 5 ms until it returns true or `timeout`
// passes. Returns the final value of `done`.
inline bool wait_until(const std::function<bool()>& done,
                       std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return done();
}

} // namespace audd_test

#endif // AUDD_TEST_SUPPORT_HPP

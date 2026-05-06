// SPDX-License-Identifier: MIT
// Receive AudD stream callbacks via an HTTP server (cpp-httplib).
//
//   ./streams_callback_handler   # listens on :8080/audd-callback

#include <iostream>

#include <httplib.h>

#include <audd/audd.hpp>

int main() {
    httplib::Server server;

    server.Post("/audd-callback", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto ev = audd::handle_callback(req.body);
            std::visit([&](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, audd::StreamCallbackMatch>) {
                    std::cout << "recognized: " << v.song.artist
                              << " — " << v.song.title
                              << " (radio " << v.radio_id
                              << ", score " << v.song.score << ")\n";
                    for (const auto& alt : v.alternatives) {
                        std::cout << "  alt: " << alt.artist
                                  << " — " << alt.title << "\n";
                    }
                } else if constexpr (std::is_same_v<T, audd::StreamCallbackNotification>) {
                    std::cout << "notification " << v.notification_code
                              << ": " << v.notification_message << "\n";
                }
            }, ev);
            res.status = 200;
        } catch (const audd::AuddError& e) {
            res.status = 400;
            res.set_content(e.what(), "text/plain");
        }
    });

    std::cout << "listening on :8080\n";
    server.listen("0.0.0.0", 8080);
    return 0;
}

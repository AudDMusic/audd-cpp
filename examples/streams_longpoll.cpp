// SPDX-License-Identifier: MIT
// Receive recognition events without a callback URL by long-polling.
//
//   AUDD_API_TOKEN=your-token ./streams_longpoll <radio_id>

#include <atomic>
#include <iostream>
#include <thread>

#include <audd/audd.hpp>

int main(int argc, char** argv) {
    int radio_id = (argc > 1) ? std::atoi(argv[1]) : 1;
    audd::AudD client(""); // AUDD_API_TOKEN

    auto category = client.streams().derive_longpoll_category(radio_id);
    std::cout << "category=" << category << "\n";

    try {
        auto poll = client.streams().longpoll(category);

        // Callback-style consumption: blocks until a terminal error fires.
        poll.run(
            [](audd::StreamCallbackMatch m) {
                std::cout << "matched: " << m.song.artist
                          << " — " << m.song.title << "\n";
            },
            [](audd::StreamCallbackNotification n) {
                std::cout << "notification: " << n.notification_message << "\n";
            },
            [](std::exception_ptr ep) {
                try { std::rethrow_exception(ep); }
                catch (const std::exception& e) {
                    std::cerr << "longpoll error: " << e.what() << "\n";
                }
            });
    } catch (const audd::AudDError& e) {
        std::cerr << "audd: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

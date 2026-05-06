// SPDX-License-Identifier: MIT
// One-shot setup: register a callback URL, subscribe to a stream, list
// what's running.
//
//   AUDD_API_TOKEN=aud_xxx ./streams_setup https://your.app/audd-callback

#include <iostream>

#include <audd/audd.hpp>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <callback-url>\n";
        return 2;
    }
    audd::AudD client(""); // pulls AUDD_API_TOKEN
    try {
        client.streams().set_callback_url(argv[1]);

        audd::AddStreamRequest req;
        req.url = "twitch:somechannel";
        req.radio_id = 1;
        client.streams().add(req);

        for (const auto& s : client.streams().list()) {
            std::cout << s.radio_id << " " << s.url
                      << " running=" << s.stream_running << "\n";
        }
    } catch (const audd::AuddError& e) {
        std::cerr << "audd: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

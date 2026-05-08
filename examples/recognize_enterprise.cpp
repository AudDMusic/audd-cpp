// SPDX-License-Identifier: MIT
// Recognize a long file via the enterprise endpoint. Returns all matches
// (multiple chunks may match different songs).
//
//   ./recognize_enterprise https://example.com/long.mp3

#include <iostream>

#include <audd/audd.hpp>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <url-or-path>\n";
        return 2;
    }

    audd::AudD client("test");
    try {
        audd::EnterpriseOptions opts;
        opts.limit = 1; // hard rule: examples pass limit=1 during dev
        auto matches = client.recognize_enterprise(std::string{argv[1]}, opts);
        int i = 1;
        for (const auto& m : matches) {
            std::cout << i++ << ". " << m.artist << " — " << m.title
                      << " (timecode " << m.timecode << ")\n";
        }
    } catch (const audd::AudDError& e) {
        std::cerr << "audd: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

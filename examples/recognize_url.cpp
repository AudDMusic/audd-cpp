// SPDX-License-Identifier: MIT
// Identify a song hosted at an HTTP(S) URL.
//
//   ./recognize_url https://audd.tech/example.mp3

#include <iostream>

#include <audd/audd.hpp>

int main(int argc, char** argv) {
    const std::string source = (argc > 1) ? argv[1] : "https://audd.tech/example.mp3";

    audd::AudD client("test");
    try {
        auto result = client.recognize(source);
        if (!result) {
            std::cout << "no match\n";
            return 0;
        }
        std::cout << result->artist << " — " << result->title << "\n";
        std::cout << "song link: " << result->song_link << "\n";
    } catch (const audd::AuddError& e) {
        std::cerr << "audd: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

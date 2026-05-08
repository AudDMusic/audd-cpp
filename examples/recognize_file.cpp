// SPDX-License-Identifier: MIT
// Identify a song from a local audio file (any format the AudD service
// accepts: mp3, m4a, wav, ogg, ...).
//
//   ./recognize_file /path/to/clip.mp3

#include <iostream>

#include <audd/audd.hpp>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <audio-file>\n";
        return 2;
    }
    audd::AudD client("test");
    try {
        auto result = client.recognize(audd::SourceFilePath{argv[1]});
        if (!result) {
            std::cout << "no match\n";
            return 0;
        }
        std::cout << result->artist << " — " << result->title << "\n";
    } catch (const audd::AudDError& e) {
        std::cerr << "audd: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

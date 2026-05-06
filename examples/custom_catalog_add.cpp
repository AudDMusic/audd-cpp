// SPDX-License-Identifier: MIT
// Add a song to your private fingerprint catalog.
//
// NOTE: this is *not* music recognition — it pre-fingerprints YOUR audio
// for AudD to identify later, on YOUR account only. Custom-catalog access
// requires a separate subscription (api@audd.io).
//
//   AUDD_API_TOKEN=aud_xxx ./custom_catalog_add 42 /path/to/track.mp3

#include <iostream>

#include <audd/audd.hpp>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " <audio_id> <file-or-url>\n";
        return 2;
    }
    int audio_id = std::atoi(argv[1]);
    audd::AudD client("");

    try {
        client.custom_catalog().add(audio_id, std::string{argv[2]});
        std::cout << "added audio_id=" << audio_id << "\n";
    } catch (const audd::AuddCustomCatalogAccessError& e) {
        std::cerr << e.what() << "\n";
        return 3;
    } catch (const audd::AuddError& e) {
        std::cerr << "audd: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

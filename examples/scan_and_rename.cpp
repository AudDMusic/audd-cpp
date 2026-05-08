// SPDX-License-Identifier: MIT
// Walk a directory of audio files, identify each, and rename the file to
// "<Artist> - <Title>.<ext>". File-only rename — no audio-tag write (use a
// dedicated tagging tool like TagLib if you want metadata in the file
// itself).
//
//   AUDD_API_TOKEN=your-token ./scan_and_rename /path/to/library

#include <filesystem>
#include <iostream>
#include <string>

#include <audd/audd.hpp>

namespace fs = std::filesystem;

namespace {

bool is_audio(const fs::path& p) {
    auto ext = p.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".mp3" || ext == ".m4a" || ext == ".flac" || ext == ".ogg" || ext == ".wav";
}

std::string sanitize(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') out.push_back('_');
        else out.push_back(c);
    }
    return out;
}

} // anonymous

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <directory>\n";
        return 2;
    }
    audd::AudD client("");
    for (const auto& entry : fs::recursive_directory_iterator(argv[1])) {
        if (!entry.is_regular_file()) continue;
        if (!is_audio(entry.path())) continue;
        try {
            auto r = client.recognize(audd::SourceFilePath{entry.path().string()});
            if (!r || r->artist.empty() || r->title.empty()) {
                std::cout << "skipped: " << entry.path() << " (no match)\n";
                continue;
            }
            std::string newname = sanitize(r->artist) + " - " + sanitize(r->title) +
                                  entry.path().extension().string();
            fs::path target = entry.path().parent_path() / newname;
            if (target == entry.path()) continue;
            fs::rename(entry.path(), target);
            std::cout << entry.path() << " -> " << target << "\n";
        } catch (const audd::AudDError& e) {
            std::cerr << "skipped: " << entry.path() << " (" << e.what() << ")\n";
        }
    }
    return 0;
}

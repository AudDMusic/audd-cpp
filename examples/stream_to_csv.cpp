// SPDX-License-Identifier: MIT
// Append every recognition match from a long-poll stream to a CSV file.
//
//   AUDD_API_TOKEN=your-token ./stream_to_csv 1 /tmp/audd-matches.csv

#include <fstream>
#include <iostream>

#include <audd/audd.hpp>

namespace {

std::string csv_escape(const std::string& s) {
    bool needs = s.find(',') != std::string::npos ||
                 s.find('"') != std::string::npos ||
                 s.find('\n') != std::string::npos;
    if (!needs) return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out.push_back('"');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

} // anonymous

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " <radio_id> <out.csv>\n";
        return 2;
    }
    int radio_id = std::atoi(argv[1]);
    audd::AudD client("");

    std::ofstream out(argv[2], std::ios::app);
    if (!out) { std::cerr << "cannot open output\n"; return 1; }
    if (out.tellp() == 0) {
        out << "timestamp,radio_id,artist,title,score,song_link\n";
    }

    auto category = client.streams().derive_longpoll_category(radio_id);
    auto poll = client.streams().longpoll(category);
    poll.run(
        [&](audd::StreamCallbackMatch m) {
            out << csv_escape(m.timestamp) << ',' << m.radio_id << ','
                << csv_escape(m.song.artist) << ',' << csv_escape(m.song.title) << ','
                << m.song.score << ',' << csv_escape(m.song.song_link) << '\n';
            out.flush();
        },
        {},
        [](std::exception_ptr ep) {
            try { std::rethrow_exception(ep); }
            catch (const std::exception& e) {
                std::cerr << "longpoll error: " << e.what() << "\n";
            }
        });
    return 0;
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#ifndef AUDD_ADVANCED_HPP
#define AUDD_ADVANCED_HPP

#include <future>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <audd/recognition.hpp>

namespace audd {

class AudD; // forward

// AdvancedClient is the escape-hatch namespace: typed lyrics search and a
// raw-method-name request helper. Reach via AudD::advanced().
class AdvancedClient {
public:
    explicit AdvancedClient(AudD* parent) noexcept : parent_(parent) {}

    // find_lyrics searches AudD's lyrics database. Returns an empty vector
    // on no match.
    std::vector<LyricsResult> find_lyrics(const std::string& query);
    std::future<std::vector<LyricsResult>> find_lyrics_async(std::string query);

    // raw_request hits api.audd.io/<method>/ with the given form params and
    // returns the parsed JSON body. Use for AudD endpoints not yet wrapped
    // by a typed method on this SDK.
    nlohmann::json raw_request(const std::string& method,
                               const std::map<std::string, std::string>& params);
    std::future<nlohmann::json> raw_request_async(
        std::string method,
        std::map<std::string, std::string> params);

private:
    AudD* parent_;
};

} // namespace audd

#endif // AUDD_ADVANCED_HPP

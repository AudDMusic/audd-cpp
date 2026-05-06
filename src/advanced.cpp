// SPDX-License-Identifier: MIT
// Copyright (c) AudD <https://audd.io>

#include <audd/advanced.hpp>

#include <string>

#include <audd/client.hpp>
#include <audd/error.hpp>

#include "internal/client_internal.hpp"
#include "internal/http_client.hpp"
#include "internal/json_parse.hpp"

namespace audd {

namespace {
constexpr const char* kApiBase = "https://api.audd.io";
}

std::vector<LyricsResult> AdvancedClient::find_lyrics(const std::string& query) {
    auto body = raw_request("findLyrics", {{"q", query}});
    auto it = body.find("result");
    std::vector<LyricsResult> out;
    if (it == body.end() || it->is_null()) return out;
    if (!it->is_array()) {
        throw AuddSerializationError("findLyrics result is not an array", it->dump());
    }
    for (const auto& e : *it) out.push_back(internal::parse_lyrics(e));
    return out;
}

std::future<std::vector<LyricsResult>>
AdvancedClient::find_lyrics_async(std::string query) {
    return std::async(std::launch::async, [this, query = std::move(query)]() {
        return this->find_lyrics(query);
    });
}

nlohmann::json AdvancedClient::raw_request(
    const std::string& method,
    const std::map<std::string, std::string>& params) {
    internal::FormFields f;
    f.data = params;
    return parent_->internal()->post_form(
        std::string(kApiBase) + "/" + method + "/", f);
}

std::future<nlohmann::json>
AdvancedClient::raw_request_async(std::string method,
                                  std::map<std::string, std::string> params) {
    return std::async(std::launch::async,
        [this, method = std::move(method), params = std::move(params)]() {
            return this->raw_request(method, params);
        });
}

} // namespace audd

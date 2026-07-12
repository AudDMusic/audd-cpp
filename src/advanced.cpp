// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

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
    std::vector<LyricsResult> out;
    const auto* result_arr = internal::result_array_or_null(body);
    if (!result_arr) return out;
    for (const auto& e : *result_arr) out.push_back(internal::parse_lyrics(e));
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

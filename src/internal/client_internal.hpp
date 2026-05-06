// SPDX-License-Identifier: MIT
// Copyright (c) AudD <https://audd.io>
//
// AudD::Internal definition. Sub-clients pull this in to access the
// shared HTTP plumbing.

#ifndef AUDD_INTERNAL_CLIENT_INTERNAL_HPP
#define AUDD_INTERNAL_CLIENT_INTERNAL_HPP

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include <audd/client.hpp>
#include "internal/http_client.hpp"

namespace audd {

struct AudD::Internal {
    ClientConfig                                config;
    std::unique_ptr<internal::HttpClient>       standard_http;
    std::unique_ptr<internal::HttpClient>       enterprise_http;
    mutable std::mutex                          token_mutex;
    std::string                                 api_token;

    nlohmann::json post_form(const std::string& url, internal::FormFields fields,
                             bool custom_catalog_ctx = false);
    nlohmann::json get(const std::string& url,
                       const std::map<std::string, std::string>& params);

    void emit_event(AuddEvent::Kind kind,
                    const std::string& method,
                    const std::string& url,
                    const std::string& request_id,
                    int http_status,
                    std::chrono::milliseconds elapsed,
                    int error_code);
};

} // namespace audd

#endif // AUDD_INTERNAL_CLIENT_INTERNAL_HPP

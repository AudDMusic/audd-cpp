// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)
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
#include "internal/retry.hpp"

namespace audd {

struct AudD::Internal {
    ClientConfig                                config;
    std::unique_ptr<internal::HttpClient>       standard_http;
    std::unique_ptr<internal::HttpClient>       enterprise_http;
    mutable std::mutex                          token_mutex;
    std::string                                 api_token;

    // POSTs default to RetryGate::PreUploadOnly: a POST is assumed to be
    // metered or mutating unless the call site says otherwise, so a failure
    // after the body may have reached the server is never retried.
    // Idempotent read endpoints served over POST (getStreams, getCallbackUrl)
    // pass RetryGate::AllConnectionErrors to keep full retry.
    nlohmann::json post_form(const std::string& url, internal::FormFields fields,
                             bool custom_catalog_ctx = false,
                             internal::RetryGate gate = internal::RetryGate::PreUploadOnly);
    // post_form overload that overrides max_attempts. custom_catalog().add
    // passes max_attempts=1: an upload is sent once and never retried
    // automatically.
    nlohmann::json post_form(const std::string& url, internal::FormFields fields,
                             bool custom_catalog_ctx, int max_attempts,
                             internal::RetryGate gate = internal::RetryGate::PreUploadOnly);
    nlohmann::json get(const std::string& url,
                       const std::map<std::string, std::string>& params);

    void emit_event(AudDEvent::Kind kind,
                    const std::string& method,
                    const std::string& url,
                    const std::string& request_id,
                    int http_status,
                    std::chrono::milliseconds elapsed,
                    int error_code);
};

namespace internal {

// decode_or_throw returns the parsed body of a `status: success` response,
// or throws the matching typed error. A code-51 warning on a successful
// response (a top-level `warning` object, or an `error` block next to a
// usable result) is passed to on_deprecation.
nlohmann::json decode_or_throw(HttpResponse& resp,
                               const std::function<void(const std::string&)>& on_deprecation,
                               bool custom_catalog_context = false);

} // namespace internal

} // namespace audd

#endif // AUDD_INTERNAL_CLIENT_INTERNAL_HPP

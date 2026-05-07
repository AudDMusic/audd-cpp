// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)
//
// Internal HTTP client wrapping libcurl. Not part of the public API.

#ifndef AUDD_INTERNAL_HTTP_CLIENT_HPP
#define AUDD_INTERNAL_HTTP_CLIENT_HPP

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

namespace audd::internal {

// FileField is a multipart-file upload field (name, content-type, content
// from path or memory).
struct FileField {
    std::string name;        // file display name
    std::string content_type;
    std::string path;        // either path...
    std::vector<std::uint8_t> bytes; // ...or bytes
    bool is_path() const noexcept { return !path.empty(); }
};

// FormFields aggregates a multipart POST request: data fields + at most one
// file field. If `file` is nullopt and `data` has only a `url` key, the
// request is sent as application/x-www-form-urlencoded; otherwise as
// multipart/form-data.
struct FormFields {
    std::map<std::string, std::string> data;
    std::optional<FileField>           file;
};

// HttpResponse is the raw HTTP-level response.
struct HttpResponse {
    int            http_status = 0;
    std::string    raw_body;
    nlohmann::json json_body;     // parsed body, null if non-JSON
    std::string    request_id;    // X-Request-Id header value
};

// HttpClient is a thin wrapper around libcurl, hiding the C handle and
// exposing form/multipart POST + GET. Reused across requests via a
// mutex-guarded easy-handle pool.
class HttpClient {
public:
    HttpClient(std::string api_token, std::chrono::milliseconds timeout);
    ~HttpClient();
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    void set_api_token(const std::string& new_token);
    std::string api_token() const;

    // post_form executes a multipart-or-urlencoded POST and returns the
    // response. Adds api_token to fields.data if not already set.
    HttpResponse post_form(const std::string& url, FormFields fields);

    // get executes a GET with the given query params.
    HttpResponse get(const std::string& url,
                     const std::map<std::string, std::string>& params);

    // user_agent is the User-Agent string used by every request.
    static std::string user_agent();

private:
    std::string                         api_token_;
    std::chrono::milliseconds           timeout_;
    mutable std::mutex                  token_mutex_;
};

// libcurl global init. Call once per process. Safe to call multiple times.
void global_init();

} // namespace audd::internal

#endif // AUDD_INTERNAL_HTTP_CLIENT_HPP

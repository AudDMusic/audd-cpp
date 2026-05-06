// SPDX-License-Identifier: MIT
// Copyright (c) AudD <https://audd.io>

#include "internal/http_client.hpp"

#include <atomic>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include <curl/curl.h>

#include <audd/error.hpp>
#include <audd/version.hpp>

namespace audd::internal {

namespace {

std::atomic<bool> g_initialized{false};
std::mutex g_init_mutex;

// curl write callback — appends data into the std::string passed via userp.
std::size_t write_to_string(char* ptr, std::size_t size, std::size_t nmemb, void* userp) {
    auto* out = static_cast<std::string*>(userp);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

// curl header callback — captures X-Request-Id (case-insensitive).
std::size_t capture_request_id(char* buffer, std::size_t size, std::size_t nitems, void* userp) {
    auto* out = static_cast<std::string*>(userp);
    const std::size_t total = size * nitems;
    constexpr const char* kHeader = "x-request-id:";
    constexpr std::size_t kHeaderLen = 13;
    if (total > kHeaderLen) {
        std::string lower(buffer, std::min(total, kHeaderLen));
        for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower.compare(0, kHeaderLen, kHeader) == 0) {
            std::string val(buffer + kHeaderLen, total - kHeaderLen);
            // trim leading whitespace, trailing CRLF
            std::size_t b = val.find_first_not_of(" \t");
            std::size_t e = val.find_last_not_of(" \t\r\n");
            if (b != std::string::npos && e != std::string::npos && e >= b) {
                *out = val.substr(b, e - b + 1);
            }
        }
    }
    return total;
}

// url-encode a single string (using curl_easy_escape).
std::string url_encode(CURL* curl, const std::string& s) {
    char* enc = curl_easy_escape(curl, s.c_str(), static_cast<int>(s.size()));
    if (!enc) return s;
    std::string out(enc);
    curl_free(enc);
    return out;
}

// build a query string from a map. Returns "k=v&..." (no leading "?").
std::string build_query(CURL* curl, const std::map<std::string, std::string>& params) {
    std::string out;
    for (const auto& kv : params) {
        if (!out.empty()) out.push_back('&');
        out += url_encode(curl, kv.first);
        out.push_back('=');
        out += url_encode(curl, kv.second);
    }
    return out;
}

} // anonymous

void global_init() {
    if (g_initialized.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lk(g_init_mutex);
    if (g_initialized.load(std::memory_order_relaxed)) return;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        throw AuddConnectionError("curl_global_init failed");
    }
    g_initialized.store(true, std::memory_order_release);
}

std::string HttpClient::user_agent() {
    return std::string("audd-cpp/") + audd::version();
}

HttpClient::HttpClient(std::string token, std::chrono::milliseconds timeout)
    : api_token_(std::move(token)), timeout_(timeout) {
    global_init();
}

HttpClient::~HttpClient() = default;

void HttpClient::set_api_token(const std::string& new_token) {
    std::lock_guard<std::mutex> lk(token_mutex_);
    api_token_ = new_token;
}

std::string HttpClient::api_token() const {
    std::lock_guard<std::mutex> lk(token_mutex_);
    return api_token_;
}

HttpResponse HttpClient::post_form(const std::string& url, FormFields fields) {
    // Inject api_token if not already present.
    {
        std::lock_guard<std::mutex> lk(token_mutex_);
        if (!api_token_.empty() && fields.data.find("api_token") == fields.data.end()) {
            fields.data["api_token"] = api_token_;
        }
    }

    CURL* curl = curl_easy_init();
    if (!curl) throw AuddConnectionError("curl_easy_init failed");

    HttpResponse resp;
    std::string body;
    std::string request_id;
    curl_mime* mime = nullptr;
    std::string urlencoded;
    struct curl_slist* headers = nullptr;
    std::string ua = user_agent();

    try {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_USERAGENT, ua.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_string);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, capture_request_id);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &request_id);

        long timeout_ms = static_cast<long>(timeout_.count());
        if (timeout_ms <= 0) timeout_ms = 90 * 1000;
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 30L * 1000L);

        if (fields.file.has_value()) {
            // multipart upload
            mime = curl_mime_init(curl);
            for (const auto& kv : fields.data) {
                curl_mimepart* part = curl_mime_addpart(mime);
                curl_mime_name(part, kv.first.c_str());
                curl_mime_data(part, kv.second.c_str(), CURL_ZERO_TERMINATED);
            }
            curl_mimepart* filepart = curl_mime_addpart(mime);
            curl_mime_name(filepart, "file");
            curl_mime_filename(filepart, fields.file->name.c_str());
            if (!fields.file->content_type.empty()) {
                curl_mime_type(filepart, fields.file->content_type.c_str());
            }
            if (fields.file->is_path()) {
                if (curl_mime_filedata(filepart, fields.file->path.c_str()) != CURLE_OK) {
                    throw AuddConnectionError("failed to attach file: " + fields.file->path);
                }
            } else {
                curl_mime_data(filepart,
                               reinterpret_cast<const char*>(fields.file->bytes.data()),
                               fields.file->bytes.size());
            }
            curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
        } else {
            urlencoded = build_query(curl, fields.data);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, urlencoded.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(urlencoded.size()));
            headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }

        CURLcode rc = curl_easy_perform(curl);
        if (rc != CURLE_OK) {
            std::string msg = curl_easy_strerror(rc);
            if (mime) curl_mime_free(mime);
            if (headers) curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            throw AuddConnectionError(msg);
        }

        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        resp.http_status = static_cast<int>(status);
        resp.raw_body = std::move(body);
        resp.request_id = std::move(request_id);
        try {
            resp.json_body = nlohmann::json::parse(resp.raw_body);
        } catch (const std::exception&) {
            // leave json_body as null
        }

        if (mime) curl_mime_free(mime);
        if (headers) curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    } catch (...) {
        if (mime) curl_mime_free(mime);
        if (headers) curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        throw;
    }

    return resp;
}

HttpResponse HttpClient::get(const std::string& url,
                             const std::map<std::string, std::string>& params) {
    auto with_token = params;
    {
        std::lock_guard<std::mutex> lk(token_mutex_);
        if (!api_token_.empty() && with_token.find("api_token") == with_token.end()) {
            with_token["api_token"] = api_token_;
        }
    }

    CURL* curl = curl_easy_init();
    if (!curl) throw AuddConnectionError("curl_easy_init failed");

    HttpResponse resp;
    std::string body;
    std::string request_id;
    std::string ua = user_agent();

    std::string full_url = url;
    std::string q = build_query(curl, with_token);
    if (!q.empty()) {
        full_url += (full_url.find('?') == std::string::npos ? '?' : '&');
        full_url += q;
    }

    try {
        curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
        curl_easy_setopt(curl, CURLOPT_USERAGENT, ua.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_string);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, capture_request_id);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &request_id);
        long timeout_ms = static_cast<long>(timeout_.count());
        if (timeout_ms <= 0) timeout_ms = 90 * 1000;
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 30L * 1000L);

        CURLcode rc = curl_easy_perform(curl);
        if (rc != CURLE_OK) {
            std::string msg = curl_easy_strerror(rc);
            curl_easy_cleanup(curl);
            throw AuddConnectionError(msg);
        }
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        resp.http_status = static_cast<int>(status);
        resp.raw_body = std::move(body);
        resp.request_id = std::move(request_id);
        try {
            resp.json_body = nlohmann::json::parse(resp.raw_body);
        } catch (const std::exception&) {
            // leave null
        }
        curl_easy_cleanup(curl);
    } catch (...) {
        curl_easy_cleanup(curl);
        throw;
    }
    return resp;
}

} // namespace audd::internal

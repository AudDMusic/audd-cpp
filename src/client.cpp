// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#include <audd/client.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#include <audd/error.hpp>
#include "internal/client_internal.hpp"
#include "internal/http_client.hpp"
#include "internal/json_parse.hpp"
#include "internal/retry.hpp"

namespace audd {

namespace {

constexpr const char* kApiBase        = "https://api.audd.io";
constexpr const char* kEnterpriseBase = "https://enterprise.audd.io";
constexpr const char* kTokenEnvVar    = "AUDD_API_TOKEN";
constexpr int kHttpClientErrorFloor = 400;
constexpr int kDeprecatedParamsCode = 51;

// Source preparation: returns a callable that yields fresh FormFields per
// retry attempt. Mirrors the audd-go reopener pattern: per-attempt
// re-opening so we never silently send empty bodies on retry.
using SourceReopener = std::function<internal::FormFields()>;

SourceReopener prepare_source(const Source& s) {
    return std::visit([](const auto& v) -> SourceReopener {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::string>) {
            // auto-classify URL vs file path
            const std::string& sv = v;
            if (sv.rfind("http://", 0) == 0 || sv.rfind("https://", 0) == 0) {
                std::string url = sv;
                return [url]() -> internal::FormFields {
                    internal::FormFields f;
                    f.data["url"] = url;
                    return f;
                };
            }
            // file path?
            FILE* fp = std::fopen(sv.c_str(), "rb");
            if (fp) {
                std::fclose(fp);
                std::string path = sv;
                return [path]() -> internal::FormFields {
                    internal::FormFields f;
                    internal::FileField file;
                    file.name = path.substr(path.find_last_of("/\\") + 1);
                    if (file.name.empty()) file.name = "upload.bin";
                    file.content_type = "application/octet-stream";
                    file.path = path;
                    f.file = std::move(file);
                    return f;
                };
            }
            throw std::invalid_argument(
                std::string("audd: \"") + sv +
                "\" is not an HTTP URL (must start with http:// or https://) "
                "and is not an existing file path; pass a URL string, a file "
                "path string, an audd::SourceBytes, or an explicit "
                "audd::SourceUrl / audd::SourceFilePath");
        } else if constexpr (std::is_same_v<T, SourceUrl>) {
            std::string url = v.url;
            return [url]() -> internal::FormFields {
                internal::FormFields f;
                f.data["url"] = url;
                return f;
            };
        } else if constexpr (std::is_same_v<T, SourceFilePath>) {
            std::string path = v.path;
            return [path]() -> internal::FormFields {
                internal::FormFields f;
                internal::FileField file;
                file.name = path.substr(path.find_last_of("/\\") + 1);
                if (file.name.empty()) file.name = "upload.bin";
                file.content_type = "application/octet-stream";
                file.path = path;
                f.file = std::move(file);
                return f;
            };
        } else if constexpr (std::is_same_v<T, SourceBytes>) {
            SourceBytes copy = v;
            return [copy]() -> internal::FormFields {
                internal::FormFields f;
                internal::FileField file;
                file.name = copy.name;
                file.content_type = copy.mime_type;
                file.bytes = copy.bytes;
                f.file = std::move(file);
                return f;
            };
        }
    }, s);
}

std::string join_csv(const std::vector<std::string>& v) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) out.push_back(',');
        out += v[i];
    }
    return out;
}

void apply_recognize_opts(internal::FormFields& f, const RecognizeOptions& opts) {
    if (!opts.return_metadata.empty()) f.data["return"] = join_csv(opts.return_metadata);
    if (!opts.market.empty())          f.data["market"] = opts.market;
}

void apply_enterprise_opts(internal::FormFields& f, const EnterpriseOptions& opts) {
    if (!opts.return_metadata.empty()) f.data["return"] = join_csv(opts.return_metadata);
    if (opts.skip)                     f.data["skip"]   = std::to_string(*opts.skip);
    if (opts.every)                    f.data["every"]  = std::to_string(*opts.every);
    if (opts.limit)                    f.data["limit"]  = std::to_string(*opts.limit);
    if (opts.skip_first_seconds)       f.data["skip_first_seconds"] = std::to_string(*opts.skip_first_seconds);
    if (opts.use_timecode)             f.data["use_timecode"]    = (*opts.use_timecode    ? "true" : "false");
    if (opts.accurate_offsets)         f.data["accurate_offsets"] = (*opts.accurate_offsets ? "true" : "false");
}

// Retry helper now lives in internal/retry.hpp so tests can verify the
// per-call invariant directly.
using internal::retry_on_connection_error;

// Inspect an HTTP response and return the parsed JSON body, or throw the
// appropriate typed error. Implements the code-51 deprecation pass-through.
nlohmann::json decode_or_throw(internal::HttpResponse& resp,
                               const std::function<void(const std::string&)>& on_deprecation,
                               bool custom_catalog_context = false) {
    if (resp.json_body.is_null()) {
        if (resp.http_status >= kHttpClientErrorFloor) {
            AudDApiError e(0, "HTTP " + std::to_string(resp.http_status) +
                              " with non-JSON response body",
                           resp.http_status, resp.request_id);
            e.raw_response = resp.raw_body;
            throw e;
        }
        throw AudDSerializationError("Unparseable response", resp.raw_body);
    }
    nlohmann::json& body = resp.json_body;

    // code-51 strip-and-warn pass-through.
    if (body.is_object()) {
        auto err_it = body.find("error");
        if (err_it != body.end() && err_it->is_object()) {
            int code = 0;
            try { code = err_it->value("error_code", 0); } catch (...) {}
            if (code == kDeprecatedParamsCode) {
                auto result_it = body.find("result");
                if (result_it != body.end() && !result_it->is_null()) {
                    std::string msg;
                    try { msg = err_it->value("error_message", std::string()); } catch (...) {}
                    if (on_deprecation) on_deprecation(msg);
                    body.erase("error");
                    body["status"] = "success";
                }
            }
        }
        std::string status;
        try { status = body.value("status", std::string()); } catch (...) {}
        if (status == "error") {
            internal::raise_from_error_response(body, resp.http_status, resp.request_id, custom_catalog_context);
        }
        if (status == "success") return body;
        AudDApiError e(0, "Unexpected response status: " + status,
                       resp.http_status, resp.request_id);
        e.raw_response = resp.raw_body;
        throw e;
    }
    AudDApiError e(0, "Unexpected response shape (not an object)",
                   resp.http_status, resp.request_id);
    e.raw_response = resp.raw_body;
    throw e;
}

} // anonymous

// AudD::Internal helpers — declared in src/internal/client_internal.hpp.
nlohmann::json AudD::Internal::post_form(const std::string& url,
                                         internal::FormFields fields,
                                         bool custom_catalog_ctx) {
    return post_form(url, std::move(fields), custom_catalog_ctx, config.max_attempts);
}

nlohmann::json AudD::Internal::post_form(const std::string& url,
                                         internal::FormFields fields,
                                         bool custom_catalog_ctx,
                                         int max_attempts) {
    auto resp = retry_on_connection_error(max_attempts, config.backoff_factor,
        [&]() -> internal::HttpResponse {
            return standard_http->post_form(url, fields);
        });
    return decode_or_throw(resp, config.on_deprecation, custom_catalog_ctx);
}

nlohmann::json AudD::Internal::get(const std::string& url,
                                   const std::map<std::string, std::string>& params) {
    auto resp = retry_on_connection_error(config.max_attempts, config.backoff_factor,
        [&]() -> internal::HttpResponse {
            return standard_http->get(url, params);
        });
    return decode_or_throw(resp, config.on_deprecation);
}

void AudD::Internal::emit_event(AudDEvent::Kind kind,
                                const std::string& method,
                                const std::string& url,
                                const std::string& request_id,
                                int http_status,
                                std::chrono::milliseconds elapsed,
                                int error_code) {
    if (!config.on_event) return;
    try {
        AudDEvent ev;
        ev.kind = kind; ev.method = method; ev.url = url;
        ev.request_id = request_id; ev.http_status = http_status;
        ev.elapsed = elapsed; ev.error_code = error_code;
        config.on_event(ev);
    } catch (...) {}
}

AudD::AudD(const std::string& token) : AudD(token, ClientConfig{}) {}

AudD::AudD(const std::string& token, ClientConfig config) {
    internal_ = std::make_unique<Internal>();
    internal_->config = std::move(config);
    if (!internal_->config.on_deprecation) {
        internal_->config.on_deprecation = [](const std::string& msg) {
            std::cerr << "audd: deprecation: " << msg << '\n';
        };
    }
    std::string effective_token = token;
    if (effective_token.empty()) {
        const char* env = std::getenv(kTokenEnvVar);
        if (env) effective_token = env;
    }
    internal_->api_token = effective_token;
    internal_->standard_http   = std::make_unique<internal::HttpClient>(effective_token, internal_->config.standard_timeout);
    internal_->enterprise_http = std::make_unique<internal::HttpClient>(effective_token, internal_->config.enterprise_timeout);
}

AudD::~AudD() = default;

std::unique_ptr<AudD> AudD::strict(const std::string& token) {
    return strict(token, ClientConfig{});
}
std::unique_ptr<AudD> AudD::strict(const std::string& token, ClientConfig config) {
    std::string effective = token;
    if (effective.empty()) {
        const char* env = std::getenv(kTokenEnvVar);
        if (env) effective = env;
    }
    if (effective.empty()) throw AudDMissingApiTokenError{};
    return std::unique_ptr<AudD>(new AudD(effective, std::move(config)));
}

std::string AudD::api_token() const {
    std::lock_guard<std::mutex> lk(internal_->token_mutex);
    return internal_->api_token;
}

void AudD::set_api_token(const std::string& new_token) {
    if (new_token.empty()) {
        throw std::invalid_argument("audd: set_api_token requires a non-empty token");
    }
    std::lock_guard<std::mutex> lk(internal_->token_mutex);
    internal_->api_token = new_token;
    internal_->standard_http->set_api_token(new_token);
    internal_->enterprise_http->set_api_token(new_token);
}

std::optional<RecognitionResult>
AudD::recognize(const Source& source, const RecognizeOptions& opts) {
    auto reopen = prepare_source(source);
    std::string url = std::string(kApiBase) + "/";
    auto start = std::chrono::steady_clock::now();
    internal_->emit_event(AudDEvent::Kind::Request, "recognize", url, "", 0,
                          std::chrono::milliseconds(0), 0);
    internal::HttpResponse resp;
    try {
        resp = retry_on_connection_error(internal_->config.max_attempts, internal_->config.backoff_factor,
            [&]() -> internal::HttpResponse {
                auto fields = reopen();
                apply_recognize_opts(fields, opts);
                return internal_->standard_http->post_form(url, fields);
            });
    } catch (const std::exception&) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        internal_->emit_event(AudDEvent::Kind::Exception, "recognize", url, "", 0, elapsed, 0);
        throw;
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    internal_->emit_event(AudDEvent::Kind::Response, "recognize", url,
                          resp.request_id, resp.http_status, elapsed, 0);
    auto body = decode_or_throw(resp, internal_->config.on_deprecation);
    auto result_it = body.find("result");
    if (result_it == body.end() || result_it->is_null()) return std::nullopt;
    return internal::parse_recognition(*result_it);
}

std::future<std::optional<RecognitionResult>>
AudD::recognize_async(Source source, RecognizeOptions opts) {
    return std::async(std::launch::async,
        [this, source = std::move(source), opts = std::move(opts)]() {
            return this->recognize(source, opts);
        });
}

std::vector<EnterpriseMatch>
AudD::recognize_enterprise(const Source& source, const EnterpriseOptions& opts) {
    auto reopen = prepare_source(source);
    std::string url = std::string(kEnterpriseBase) + "/";
    auto start = std::chrono::steady_clock::now();
    internal_->emit_event(AudDEvent::Kind::Request, "recognize_enterprise", url, "", 0,
                          std::chrono::milliseconds(0), 0);
    internal::HttpResponse resp;
    try {
        resp = retry_on_connection_error(internal_->config.max_attempts, internal_->config.backoff_factor,
            [&]() -> internal::HttpResponse {
                auto fields = reopen();
                apply_enterprise_opts(fields, opts);
                return internal_->enterprise_http->post_form(url, fields);
            });
    } catch (const std::exception&) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        internal_->emit_event(AudDEvent::Kind::Exception, "recognize_enterprise", url, "", 0, elapsed, 0);
        throw;
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    internal_->emit_event(AudDEvent::Kind::Response, "recognize_enterprise", url,
                          resp.request_id, resp.http_status, elapsed, 0);
    auto body = decode_or_throw(resp, internal_->config.on_deprecation);
    auto result_it = body.find("result");
    std::vector<EnterpriseMatch> out;
    if (result_it == body.end() || result_it->is_null()) return out;
    if (!result_it->is_array()) {
        throw AudDSerializationError("enterprise result is not an array", result_it->dump());
    }
    for (const auto& chunk : *result_it) {
        auto parsed = internal::parse_enterprise_chunk(chunk);
        for (auto& song : parsed.songs) out.push_back(std::move(song));
    }
    return out;
}

std::future<std::vector<EnterpriseMatch>>
AudD::recognize_enterprise_async(Source source, EnterpriseOptions opts) {
    return std::async(std::launch::async,
        [this, source = std::move(source), opts = std::move(opts)]() {
            return this->recognize_enterprise(source, opts);
        });
}

StreamsClient& AudD::streams() {
    std::lock_guard<std::mutex> lk(sub_clients_mutex_);
    if (!streams_) streams_ = std::make_unique<StreamsClient>(this);
    return *streams_;
}

CustomCatalogClient& AudD::custom_catalog() {
    std::lock_guard<std::mutex> lk(sub_clients_mutex_);
    if (!custom_catalog_) custom_catalog_ = std::make_unique<CustomCatalogClient>(this);
    return *custom_catalog_;
}

AdvancedClient& AudD::advanced() {
    std::lock_guard<std::mutex> lk(sub_clients_mutex_);
    if (!advanced_) advanced_ = std::make_unique<AdvancedClient>(this);
    return *advanced_;
}

// Out-of-line definitions for RecognitionResult / EnterpriseMatch helpers.

namespace {

std::string lis_tn_streaming_url(const std::string& song_link, const char* qparam) {
    if (song_link.empty()) return "";
    auto host_start = song_link.find("//");
    if (host_start == std::string::npos) return "";
    host_start += 2;
    auto host_end = song_link.find_first_of("/?#", host_start);
    std::string host = (host_end == std::string::npos)
        ? song_link.substr(host_start)
        : song_link.substr(host_start, host_end - host_start);
    if (host != "lis.tn") return "";
    char sep = (song_link.find('?') == std::string::npos) ? '?' : '&';
    return song_link + sep + qparam;
}

} // anonymous

std::string RecognitionResult::thumbnail_url() const {
    return lis_tn_streaming_url(song_link, "thumb");
}

std::string RecognitionResult::streaming_url(StreamingProvider provider) const {
    // direct URL from metadata block (when user opted in via return_metadata)
    if (provider == StreamingProvider::AppleMusic && apple_music && !apple_music->url.empty())
        return apple_music->url;
    if (provider == StreamingProvider::Spotify && spotify) {
        // try external_urls.spotify in extras first, then uri
        auto ext_it = spotify->extras.find("external_urls");
        if (ext_it != spotify->extras.end() && ext_it->second.is_object()) {
            auto sp_it = ext_it->second.find("spotify");
            if (sp_it != ext_it->second.end() && sp_it->is_string()) {
                std::string u = sp_it->get<std::string>();
                if (!u.empty()) return u;
            }
        }
        if (!spotify->uri.empty()) return spotify->uri;
    }
    if (provider == StreamingProvider::Deezer && deezer && !deezer->link.empty())
        return deezer->link;
    if (provider == StreamingProvider::Napster && napster) {
        auto href_it = napster->extras.find("href");
        if (href_it != napster->extras.end() && href_it->second.is_string()) {
            std::string h = href_it->second.get<std::string>();
            if (!h.empty()) return h;
        }
    }
    // fallback: lis.tn redirect
    return lis_tn_streaming_url(song_link, streaming_provider_name(provider));
}

std::map<StreamingProvider, std::string> RecognitionResult::streaming_urls() const {
    std::map<StreamingProvider, std::string> out;
    for (auto p : all_streaming_providers()) {
        std::string u = streaming_url(p);
        if (!u.empty()) out.emplace(p, std::move(u));
    }
    return out;
}

std::string RecognitionResult::preview_url() const {
    if (apple_music) {
        auto p_it = apple_music->extras.find("previews");
        if (p_it != apple_music->extras.end() && p_it->second.is_array() && !p_it->second.empty()) {
            const auto& first = p_it->second[0];
            if (first.is_object()) {
                auto u_it = first.find("url");
                if (u_it != first.end() && u_it->is_string()) {
                    std::string u = u_it->get<std::string>();
                    if (!u.empty()) return u;
                }
            }
        }
    }
    if (spotify) {
        auto p_it = spotify->extras.find("preview_url");
        if (p_it != spotify->extras.end() && p_it->second.is_string()) {
            std::string u = p_it->second.get<std::string>();
            if (!u.empty()) return u;
        }
    }
    if (deezer) {
        auto p_it = deezer->extras.find("preview");
        if (p_it != deezer->extras.end() && p_it->second.is_string()) {
            std::string u = p_it->second.get<std::string>();
            if (!u.empty()) return u;
        }
    }
    return "";
}

std::string EnterpriseMatch::thumbnail_url() const {
    return lis_tn_streaming_url(song_link, "thumb");
}

std::string EnterpriseMatch::streaming_url(StreamingProvider provider) const {
    return lis_tn_streaming_url(song_link, streaming_provider_name(provider));
}

std::map<StreamingProvider, std::string> EnterpriseMatch::streaming_urls() const {
    std::map<StreamingProvider, std::string> out;
    for (auto p : all_streaming_providers()) {
        std::string u = streaming_url(p);
        if (!u.empty()) out.emplace(p, std::move(u));
    }
    return out;
}

} // namespace audd

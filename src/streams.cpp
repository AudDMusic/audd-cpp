// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#include <audd/streams.hpp>

#include <map>
#include <stdexcept>
#include <string>

#include <audd/client.hpp>
#include <audd/error.hpp>

#include "internal/client_internal.hpp"
#include "internal/http_client.hpp"
#include "internal/json_parse.hpp"

namespace audd {

namespace {

constexpr const char* kApiBase = "https://api.audd.io";
constexpr int kNoCallbackErrorCode = 19;
constexpr const char* kPreflightHint =
    "Longpoll won't deliver events because no callback URL is configured for "
    "this account. Set one first via client.streams().set_callback_url(url, "
    "opts) — `https://audd.tech/empty/` is fine if you only want longpolling "
    "and don't need a real receiver. To skip this check, set "
    "LongpollOptions.skip_callback_check = true.";

std::string add_return_to_url(const std::string& raw_url,
                              const std::vector<std::string>& return_metadata) {
    if (return_metadata.empty()) return raw_url;
    if (raw_url.find("return=") != std::string::npos) {
        // crude but correct enough: server reserved keys live near the
        // top of audd-go's check; we'll surface a typed error rather than
        // silently overwriting.
        AudDApiError e(0,
            "callback URL already contains a `return` query parameter; pass "
            "an empty return_metadata or remove the parameter from the URL",
            0, "");
        throw e;
    }
    std::string out = raw_url;
    out += (raw_url.find('?') == std::string::npos) ? '?' : '&';
    out += "return=";
    for (std::size_t i = 0; i < return_metadata.size(); ++i) {
        if (i) out.push_back(',');
        out += return_metadata[i];
    }
    return out;
}

} // anonymous

void StreamsClient::set_callback_url(const std::string& url,
                                     const SetCallbackUrlOptions& opts) {
    std::string final_url = add_return_to_url(url, opts.return_metadata);
    internal::FormFields f;
    f.data["url"] = final_url;
    parent_->internal()->post_form(std::string(kApiBase) + "/setCallbackUrl/", f);
}

std::future<void> StreamsClient::set_callback_url_async(std::string url,
                                                        SetCallbackUrlOptions opts) {
    return std::async(std::launch::async, [this, url = std::move(url), opts = std::move(opts)]() {
        this->set_callback_url(url, opts);
    });
}

std::string StreamsClient::get_callback_url() {
    auto body = parent_->internal()->post_form(std::string(kApiBase) + "/getCallbackUrl/", {});
    auto it = body.find("result");
    if (it == body.end() || it->is_null()) return "";
    if (it->is_string()) return it->get<std::string>();
    return "";
}

std::future<std::string> StreamsClient::get_callback_url_async() {
    return std::async(std::launch::async, [this]() { return this->get_callback_url(); });
}

void StreamsClient::add(const AddStreamRequest& req) {
    internal::FormFields f;
    f.data["url"]      = req.url;
    f.data["radio_id"] = std::to_string(req.radio_id);
    if (!req.callbacks.empty()) f.data["callbacks"] = req.callbacks;
    parent_->internal()->post_form(std::string(kApiBase) + "/addStream/", f);
}

std::future<void> StreamsClient::add_async(AddStreamRequest req) {
    return std::async(std::launch::async, [this, req = std::move(req)]() { this->add(req); });
}

void StreamsClient::set_url(int radio_id, const std::string& url) {
    internal::FormFields f;
    f.data["radio_id"] = std::to_string(radio_id);
    f.data["url"]      = url;
    parent_->internal()->post_form(std::string(kApiBase) + "/setStreamUrl/", f);
}

std::future<void> StreamsClient::set_url_async(int radio_id, std::string url) {
    return std::async(std::launch::async, [this, radio_id, url = std::move(url)]() {
        this->set_url(radio_id, url);
    });
}

void StreamsClient::del(int radio_id) {
    internal::FormFields f;
    f.data["radio_id"] = std::to_string(radio_id);
    parent_->internal()->post_form(std::string(kApiBase) + "/deleteStream/", f);
}

std::future<void> StreamsClient::del_async(int radio_id) {
    return std::async(std::launch::async, [this, radio_id]() { this->del(radio_id); });
}

std::vector<Stream> StreamsClient::list() {
    auto body = parent_->internal()->post_form(std::string(kApiBase) + "/getStreams/", {});
    auto it = body.find("result");
    std::vector<Stream> out;
    if (it == body.end() || it->is_null()) return out;
    if (!it->is_array()) {
        throw AudDSerializationError("getStreams result is not an array", it->dump());
    }
    for (const auto& s : *it) out.push_back(internal::parse_stream(s));
    return out;
}

std::future<std::vector<Stream>> StreamsClient::list_async() {
    return std::async(std::launch::async, [this]() { return this->list(); });
}

std::string StreamsClient::derive_longpoll_category(int radio_id) const {
    return audd::derive_longpoll_category(parent_->api_token(), radio_id);
}

LongpollPoll StreamsClient::longpoll(const std::string& category,
                                     const LongpollOptions& opts) {
    if (!opts.skip_callback_check) {
        try {
            (void)get_callback_url();
        } catch (const AudDApiError& e) {
            if (e.error_code == kNoCallbackErrorCode) {
                AudDApiError translated(0, kPreflightHint, e.http_status, e.request_id);
                throw translated;
            }
            throw;
        }
    }
    // The actual implementation lives in longpoll.cpp; we expose a factory
    // that takes the parent + parameters.
    extern LongpollPoll start_longpoll_(AudD* parent,
                                        std::string category,
                                        LongpollOptions opts);
    return start_longpoll_(parent_, category, opts);
}

LongpollPoll StreamsClient::longpoll(int radio_id, const LongpollOptions& opts) {
    return longpoll(derive_longpoll_category(radio_id), opts);
}

} // namespace audd

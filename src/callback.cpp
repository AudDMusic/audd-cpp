// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#include <audd/callback.hpp>

#include <stdexcept>
#include <string>

#include <audd/error.hpp>
#include "internal/json_parse.hpp"

namespace audd {

CallbackEvent parse_callback(const std::string& body) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(body);
    } catch (const std::exception& e) {
        throw AudDSerializationError(
            std::string("callback body is not valid JSON: ") + e.what(),
            body);
    }
    if (!j.is_object()) {
        throw AudDSerializationError("callback body is not a JSON object", body);
    }
    auto notif_it = j.find("notification");
    if (notif_it != j.end()) {
        int outer_time = 0;
        auto t_it = j.find("time");
        if (t_it != j.end() && !t_it->is_null()) {
            try { outer_time = t_it->get<int>(); } catch (...) {}
        }
        return internal::parse_stream_callback_notification(*notif_it, outer_time, body);
    }
    auto result_it = j.find("result");
    if (result_it != j.end()) {
        return internal::parse_stream_callback_match(*result_it, body);
    }
    throw AudDSerializationError("callback body has neither result nor notification", body);
}

CallbackEvent parse_callback(const char* data, std::size_t length) {
    return parse_callback(std::string(data, length));
}

} // namespace audd

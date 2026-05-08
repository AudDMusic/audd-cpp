// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#ifndef AUDD_CALLBACK_HPP
#define AUDD_CALLBACK_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include <audd/recognition.hpp>

namespace audd {

// StreamCallbackSong is one candidate song in a recognition match. Almost
// every match has exactly one Song; multiple candidates only appear when the
// same fingerprint resolves to several near-identical catalog records — and
// the extra candidates may have different artist or title (variant releases,
// near-duplicates).
struct StreamCallbackSong {
    int         score = 0;
    std::string artist;
    std::string title;
    std::string album;
    std::string release_date;
    std::string label;
    std::string song_link;
    std::string isrc;
    std::string upc;

    std::optional<AppleMusicMetadata> apple_music;
    std::optional<SpotifyMetadata>    spotify;
    std::optional<DeezerMetadata>     deezer;
    std::optional<NapsterMetadata>    napster;
    std::vector<MusicBrainzEntry>     musicbrainz;

    std::map<std::string, nlohmann::json> extras;
};

// StreamCallbackMatch is one recognition event from a stream callback or
// longpoll. Carries the top match in `song`; extra candidates (which may
// have a different artist or title — variant releases, near-duplicates) live
// in `alternatives`.
struct StreamCallbackMatch {
    std::int64_t radio_id    = 0;
    std::string  timestamp;
    int          play_length = 0;

    StreamCallbackSong              song;
    std::vector<StreamCallbackSong> alternatives;

    std::map<std::string, nlohmann::json> extras;
    std::string                           raw_response;
};

// StreamCallbackNotification is the lifecycle-event variant of a stream
// callback (e.g. "stream stopped", "can't connect").
struct StreamCallbackNotification {
    int                 radio_id = 0;
    std::optional<bool> stream_running;
    int                 notification_code = 0;
    std::string         notification_message;
    int                 time = 0; // outer `time` field (not nested under `notification`)

    std::map<std::string, nlohmann::json> extras;
    std::string                           raw_response;
};

// CallbackEvent is the typed result of parse_callback / handle_callback —
// exactly one of (match, notification). std::variant<...> guarantees
// exhaustive handling at the call site.
using CallbackEvent = std::variant<StreamCallbackMatch, StreamCallbackNotification>;

// parse_callback parses an AudD callback POST body into a typed event.
// Recognition callbacks have an outer `result` block; notification
// callbacks have a `notification` block; the discrimination is by-key.
//
// Throws AudDSerializationError on malformed input.
CallbackEvent parse_callback(const std::string& body);

// parse_callback overload that accepts raw bytes (e.g. from a frame buffer).
CallbackEvent parse_callback(const char* data, std::size_t length);

// handle_callback reads a callback POST body from a string and parses it.
// Convenience overload for callers integrating with their own HTTP server.
//
// For cpp-httplib server callers, see audd/httplib_handle_callback.hpp.
inline CallbackEvent handle_callback(const std::string& body) {
    return parse_callback(body);
}

// Convenience predicates / accessors for the CallbackEvent variant.
inline bool is_match(const CallbackEvent& e) noexcept {
    return std::holds_alternative<StreamCallbackMatch>(e);
}
inline bool is_notification(const CallbackEvent& e) noexcept {
    return std::holds_alternative<StreamCallbackNotification>(e);
}
inline const StreamCallbackMatch* match_or_null(const CallbackEvent& e) noexcept {
    return std::get_if<StreamCallbackMatch>(&e);
}
inline const StreamCallbackNotification* notification_or_null(const CallbackEvent& e) noexcept {
    return std::get_if<StreamCallbackNotification>(&e);
}

} // namespace audd

#endif // AUDD_CALLBACK_HPP

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#ifndef AUDD_INTERNAL_JSON_PARSE_HPP
#define AUDD_INTERNAL_JSON_PARSE_HPP

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include <audd/callback.hpp>
#include <audd/recognition.hpp>

namespace audd::internal {

// Parsing helpers — convert nlohmann::json blobs into the typed structs.

RecognitionResult                   parse_recognition(const nlohmann::json& j);
EnterpriseChunkResult               parse_enterprise_chunk(const nlohmann::json& j);
LyricsResult                        parse_lyrics(const nlohmann::json& j);
Stream                              parse_stream(const nlohmann::json& j);

AppleMusicMetadata                  parse_apple_music(const nlohmann::json& j);
SpotifyMetadata                     parse_spotify(const nlohmann::json& j);
DeezerMetadata                      parse_deezer(const nlohmann::json& j);
NapsterMetadata                     parse_napster(const nlohmann::json& j);
MusicBrainzEntry                    parse_musicbrainz(const nlohmann::json& j);

StreamCallbackSong                  parse_stream_callback_song(const nlohmann::json& j);
StreamCallbackMatch                 parse_stream_callback_match(const nlohmann::json& j_result, const std::string& full_body);
StreamCallbackNotification          parse_stream_callback_notification(const nlohmann::json& j_notification, int outer_time, const std::string& full_body);

// raise_from_error_response inspects a {status: error} body and throws the
// appropriate typed AudDApiError / AudDCustomCatalogAccessError.
[[noreturn]] void raise_from_error_response(
    const nlohmann::json& body,
    int http_status,
    const std::string& request_id,
    bool custom_catalog_context);

// branded_message extracts an "Artist — Title" string from a result map, if any.
std::string branded_message(const nlohmann::json& result);

// offset_to_seconds parses an AudD chunk offset string into seconds. Accepts
// "SS", "MM:SS", "HH:MM:SS", or a bare number. Returns std::nullopt on empty
// or unparseable input. Never throws.
std::optional<double> offset_to_seconds(const std::string& offset);

// extract_extras returns the subset of `obj` whose keys are NOT in `known`.
std::map<std::string, nlohmann::json> extract_extras(
    const nlohmann::json& obj,
    const std::vector<std::string>& known);

// result_array_or_null returns a pointer to `body["result"]` when it is
// present and a JSON array, and nullptr otherwise (result absent, null, or a
// wrong JSON type). Lets endpoints that expect a list of results degrade to
// an empty result set instead of throwing on an unexpected `result` shape.
const nlohmann::json* result_array_or_null(const nlohmann::json& body);

// url_query_has_key returns true when `url`'s query string contains a key
// named exactly `key` (e.g. `?key=...` or `?a=b&key=c`), matching whole keys
// only so lookalikes like `?_key=1` do not false-positive.
bool url_query_has_key(const std::string& url, const std::string& key);

// LongpollDisposition classifies what the longpoll worker should do with a
// single completed HTTP round-trip, before the event body is parsed:
//   Terminal  — an explicit HTTP/API error; stop the poll.
//   KeepAlive — a keep-alive tick (has `timeout`, no event); advance and
//               keep polling.
//   Event     — a candidate event body; try to parse it (an unparseable or
//               unknown body is skipped, not terminal).
//   Skip      — nothing usable (e.g. empty body); keep polling.
enum class LongpollDisposition { Terminal, KeepAlive, Event, Skip };

// classify_longpoll_response decides the disposition for a completed request
// from its HTTP status and parsed JSON body. Pure; never throws.
LongpollDisposition classify_longpoll_response(int http_status,
                                               const nlohmann::json& body,
                                               bool body_empty);

} // namespace audd::internal

#endif // AUDD_INTERNAL_JSON_PARSE_HPP

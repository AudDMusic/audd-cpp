// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#ifndef AUDD_INTERNAL_JSON_PARSE_HPP
#define AUDD_INTERNAL_JSON_PARSE_HPP

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
// appropriate typed AuddApiError / AuddCustomCatalogAccessError.
[[noreturn]] void raise_from_error_response(
    const nlohmann::json& body,
    int http_status,
    const std::string& request_id,
    bool custom_catalog_context);

// branded_message extracts an "Artist — Title" string from a result map, if any.
std::string branded_message(const nlohmann::json& result);

// extract_extras returns the subset of `obj` whose keys are NOT in `known`.
std::map<std::string, nlohmann::json> extract_extras(
    const nlohmann::json& obj,
    const std::vector<std::string>& known);

} // namespace audd::internal

#endif // AUDD_INTERNAL_JSON_PARSE_HPP

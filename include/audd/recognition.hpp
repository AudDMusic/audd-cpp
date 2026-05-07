// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#ifndef AUDD_RECOGNITION_HPP
#define AUDD_RECOGNITION_HPP

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace audd {

// Forward-compat: every typed model carries an `extras` map (unknown JSON
// keys, kept as nlohmann::json) plus a `raw_response` string with the
// original JSON. New AudD response keys round-trip through these without an
// SDK release.

// AppleMusicMetadata is the Apple Music metadata block on a recognition.
// All fields are best-effort optional — the AudD payload is rich and
// changes over time.
struct AppleMusicMetadata {
    std::string artist_name;
    std::string url;
    int         duration_in_millis = 0;
    std::string name;
    std::string isrc;
    std::string album_name;
    int         track_number = 0;
    std::string composer_name;
    int         disc_number = 0;
    std::string release_date;

    std::map<std::string, nlohmann::json> extras;
    std::string                           raw_response;
};

// SpotifyMetadata is the Spotify metadata block on a recognition.
struct SpotifyMetadata {
    std::string id;
    std::string name;
    int         duration_ms = 0;
    bool        explicit_   = false; // `explicit` is reserved.
    int         popularity  = 0;
    int         track_number = 0;
    std::string type;
    std::string uri;

    std::map<std::string, nlohmann::json> extras;
    std::string                           raw_response;
};

// DeezerMetadata is the Deezer metadata block.
struct DeezerMetadata {
    int         id       = 0;
    std::string title;
    int         duration = 0;
    std::string link;

    std::map<std::string, nlohmann::json> extras;
    std::string                           raw_response;
};

// NapsterMetadata is the Napster metadata block.
struct NapsterMetadata {
    std::string id;
    std::string name;
    std::string isrc;
    std::string artist_name;
    std::string album_name;

    std::map<std::string, nlohmann::json> extras;
    std::string                           raw_response;
};

// MusicBrainzEntry is one entry in the `musicbrainz` array.
// `score` is left as raw JSON because the server returns int OR string.
struct MusicBrainzEntry {
    std::string    id;
    nlohmann::json score; // int OR string
    std::string    title;
    int            length = 0;

    std::map<std::string, nlohmann::json> extras;
    std::string                           raw_response;
};

// StreamingProvider names the streaming services reachable via the lis.tn
// `?<provider>` redirect helper.
enum class StreamingProvider {
    Spotify,
    AppleMusic,
    Deezer,
    Napster,
    YouTube,
};

// streaming_provider_name returns the canonical query-string name.
inline const char* streaming_provider_name(StreamingProvider p) noexcept {
    switch (p) {
        case StreamingProvider::Spotify:    return "spotify";
        case StreamingProvider::AppleMusic: return "apple_music";
        case StreamingProvider::Deezer:     return "deezer";
        case StreamingProvider::Napster:    return "napster";
        case StreamingProvider::YouTube:    return "youtube";
    }
    return "";
}

// all_streaming_providers is the canonical iteration order.
inline const std::vector<StreamingProvider>& all_streaming_providers() {
    static const std::vector<StreamingProvider> v = {
        StreamingProvider::Spotify,
        StreamingProvider::AppleMusic,
        StreamingProvider::Deezer,
        StreamingProvider::Napster,
        StreamingProvider::YouTube,
    };
    return v;
}

// RecognitionResult is the typed result of AudD::recognize. Public-DB matches
// populate artist/title/etc.; custom-DB matches populate audio_id instead.
// Use is_public_match() / is_custom_match() to discriminate.
struct RecognitionResult {
    std::string                       timecode;
    std::optional<int>                audio_id;
    std::string                       artist;
    std::string                       title;
    std::string                       album;
    std::string                       release_date;
    std::string                       label;
    std::string                       song_link;
    std::string                       isrc;
    std::string                       upc;
    std::optional<AppleMusicMetadata> apple_music;
    std::optional<SpotifyMetadata>    spotify;
    std::optional<DeezerMetadata>     deezer;
    std::optional<NapsterMetadata>    napster;
    std::vector<MusicBrainzEntry>     musicbrainz;

    std::map<std::string, nlohmann::json> extras;
    std::string                           raw_response;

    // is_custom_match reports whether this is a custom-DB match (audio_id populated).
    bool is_custom_match() const noexcept { return audio_id.has_value(); }

    // is_public_match reports whether this is a public-DB match
    // (artist/title set, audio_id absent).
    bool is_public_match() const noexcept {
        return !audio_id.has_value() && (!artist.empty() || !title.empty());
    }

    // thumbnail_url returns the cover-art URL for lis.tn-hosted song_links;
    // empty for non-lis.tn hosts (e.g. YouTube).
    std::string thumbnail_url() const;

    // streaming_url returns a direct or redirect URL for a streaming
    // provider.
    //
    // Resolution order:
    //  1. Direct URL from the metadata block (apple_music.url,
    //     spotify.external_urls.spotify, deezer.link, napster.href) when
    //     the user requested that provider via opts.return_metadata.
    //     Direct = no redirect, faster.
    //  2. lis.tn redirect "<song_link>?<provider>" when song_link is on
    //     lis.tn.
    //  3. "" otherwise. YouTube has no metadata-block fallback.
    std::string streaming_url(StreamingProvider provider) const;

    // streaming_urls returns the union of all providers with a resolvable
    // URL.
    std::map<StreamingProvider, std::string> streaming_urls() const;

    // preview_url returns the first available 30-second preview URL across
    // apple_music.previews[0].url -> spotify.preview_url -> deezer.preview,
    // in that priority order. Empty if no metadata block carries a preview.
    //
    // Note: previews are governed by the respective providers' terms of
    // use. The SDK consumer is responsible for honoring caching /
    // attribution / redistribution constraints.
    std::string preview_url() const;
};

// EnterpriseMatch is one match within a chunk of the enterprise endpoint's
// response. Multiple matches per file are common.
struct EnterpriseMatch {
    int         score = 0;
    std::string timecode;
    std::string artist;
    std::string title;
    std::string album;
    std::string release_date;
    std::string label;
    std::string isrc;
    std::string upc;
    std::string song_link;
    int         start_offset = 0;
    int         end_offset   = 0;

    std::map<std::string, nlohmann::json> extras;
    std::string                           raw_response;

    // thumbnail_url returns the cover-art URL for lis.tn-hosted song_links.
    std::string thumbnail_url() const;

    // streaming_url returns the lis.tn redirect URL for a streaming
    // provider, or "" when song_link is non-lis.tn (EnterpriseMatch doesn't
    // have full metadata blocks).
    std::string streaming_url(StreamingProvider provider) const;

    // streaming_urls returns all providers with a lis.tn redirect URL.
    std::map<StreamingProvider, std::string> streaming_urls() const;
};

// EnterpriseChunkResult wraps an array of matches for a single processed
// chunk of the enterprise upload (the response has one element per chunk).
struct EnterpriseChunkResult {
    std::vector<EnterpriseMatch> songs;
    std::string                  offset;

    std::map<std::string, nlohmann::json> extras;
    std::string                           raw_response;
};

// LyricsResult is one entry in the findLyrics response array.
struct LyricsResult {
    std::string artist;
    std::string title;
    std::string lyrics;
    int         song_id = 0;
    std::string media;
    std::string full_title;
    int         artist_id = 0;
    std::string song_link;

    std::map<std::string, nlohmann::json> extras;
    std::string                           raw_response;
};

// Stream describes one running stream subscription.
struct Stream {
    int         radio_id       = 0;
    std::string url;
    bool        stream_running = false;
    std::string longpoll_category;

    std::map<std::string, nlohmann::json> extras;
    std::string                           raw_response;
};

// RecognizeOptions controls the standard recognize endpoint.
struct RecognizeOptions {
    // return_metadata is the list of metadata sources to include.
    // Valid values: "apple_music", "spotify", "deezer", "napster", "musicbrainz".
    std::vector<std::string> return_metadata;
    // market is the ISO country code (server default: "us").
    std::string market;
};

// EnterpriseOptions controls the enterprise recognize endpoint.
struct EnterpriseOptions {
    std::vector<std::string> return_metadata;
    std::optional<int>       skip;
    std::optional<int>       every;
    std::optional<int>       limit;
    std::optional<int>       skip_first_seconds;
    std::optional<bool>      use_timecode;
    std::optional<bool>      accurate_offsets;
};

} // namespace audd

#endif // AUDD_RECOGNITION_HPP

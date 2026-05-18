// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#include "internal/json_parse.hpp"

#include <algorithm>
#include <set>
#include <string>

#include <audd/error.hpp>

namespace audd::internal {

namespace {

// j_get returns the field at `key` cast to T, or default_value on missing /
// wrong type / null. Tolerant of the AudD API's loose JSON shapes.
template <typename T>
T j_get(const nlohmann::json& j, const std::string& key, T default_value) {
    if (!j.is_object()) return default_value;
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return default_value;
    try {
        return it->get<T>();
    } catch (const std::exception&) {
        return default_value;
    }
}

// j_get_string is the common case: pull a string field, returning "" on
// missing.
std::string j_str(const nlohmann::json& j, const std::string& key) {
    return j_get<std::string>(j, key, "");
}

int j_int(const nlohmann::json& j, const std::string& key) {
    return j_get<int>(j, key, 0);
}

bool j_bool(const nlohmann::json& j, const std::string& key) {
    return j_get<bool>(j, key, false);
}

std::int64_t j_int64(const nlohmann::json& j, const std::string& key) {
    return j_get<std::int64_t>(j, key, 0);
}

} // anonymous

std::map<std::string, nlohmann::json> extract_extras(
    const nlohmann::json& obj,
    const std::vector<std::string>& known) {
    std::map<std::string, nlohmann::json> out;
    if (!obj.is_object()) return out;
    std::set<std::string> known_set(known.begin(), known.end());
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (known_set.count(it.key()) == 0) {
            out.emplace(it.key(), it.value());
        }
    }
    return out;
}

AppleMusicMetadata parse_apple_music(const nlohmann::json& j) {
    AppleMusicMetadata m;
    m.artist_name        = j_str(j, "artistName");
    m.url                = j_str(j, "url");
    m.duration_in_millis = j_int(j, "durationInMillis");
    m.name               = j_str(j, "name");
    m.isrc               = j_str(j, "isrc");
    m.album_name         = j_str(j, "albumName");
    m.track_number       = j_int(j, "trackNumber");
    m.composer_name      = j_str(j, "composerName");
    m.disc_number        = j_int(j, "discNumber");
    m.release_date       = j_str(j, "releaseDate");
    m.extras = extract_extras(j, {
        "artistName", "url", "durationInMillis", "name", "isrc",
        "albumName", "trackNumber", "composerName", "discNumber", "releaseDate",
    });
    m.raw_response = j.dump();
    return m;
}

SpotifyMetadata parse_spotify(const nlohmann::json& j) {
    SpotifyMetadata m;
    m.id           = j_str(j, "id");
    m.name         = j_str(j, "name");
    m.duration_ms  = j_int(j, "duration_ms");
    m.explicit_    = j_bool(j, "explicit");
    m.popularity   = j_int(j, "popularity");
    m.track_number = j_int(j, "track_number");
    m.type         = j_str(j, "type");
    m.uri          = j_str(j, "uri");
    m.extras = extract_extras(j, {
        "id", "name", "duration_ms", "explicit", "popularity",
        "track_number", "type", "uri",
    });
    m.raw_response = j.dump();
    return m;
}

DeezerMetadata parse_deezer(const nlohmann::json& j) {
    DeezerMetadata m;
    m.id       = j_int(j, "id");
    m.title    = j_str(j, "title");
    m.duration = j_int(j, "duration");
    m.link     = j_str(j, "link");
    m.extras = extract_extras(j, {"id", "title", "duration", "link"});
    m.raw_response = j.dump();
    return m;
}

NapsterMetadata parse_napster(const nlohmann::json& j) {
    NapsterMetadata m;
    m.id          = j_str(j, "id");
    m.name        = j_str(j, "name");
    m.isrc        = j_str(j, "isrc");
    m.artist_name = j_str(j, "artistName");
    m.album_name  = j_str(j, "albumName");
    m.extras = extract_extras(j, {"id", "name", "isrc", "artistName", "albumName"});
    m.raw_response = j.dump();
    return m;
}

MusicBrainzEntry parse_musicbrainz(const nlohmann::json& j) {
    MusicBrainzEntry m;
    m.id     = j_str(j, "id");
    if (j.is_object()) {
        auto it = j.find("score");
        if (it != j.end()) m.score = *it;
    }
    m.title  = j_str(j, "title");
    m.length = j_int(j, "length");
    m.extras = extract_extras(j, {"id", "score", "title", "length"});
    m.raw_response = j.dump();
    return m;
}

RecognitionResult parse_recognition(const nlohmann::json& j) {
    RecognitionResult r;
    r.timecode     = j_str(j, "timecode");
    if (j.is_object()) {
        auto it = j.find("audio_id");
        if (it != j.end() && !it->is_null()) {
            try { r.audio_id = it->get<int>(); } catch (...) {}
        }
    }
    r.artist       = j_str(j, "artist");
    r.title        = j_str(j, "title");
    r.album        = j_str(j, "album");
    r.release_date = j_str(j, "release_date");
    r.label        = j_str(j, "label");
    r.song_link    = j_str(j, "song_link");
    r.isrc         = j_str(j, "isrc");
    r.upc          = j_str(j, "upc");

    if (j.is_object()) {
        auto it = j.find("apple_music");
        if (it != j.end() && it->is_object()) r.apple_music = parse_apple_music(*it);
        it = j.find("spotify");
        if (it != j.end() && it->is_object()) r.spotify = parse_spotify(*it);
        it = j.find("deezer");
        if (it != j.end() && it->is_object()) r.deezer = parse_deezer(*it);
        it = j.find("napster");
        if (it != j.end() && it->is_object()) r.napster = parse_napster(*it);
        it = j.find("musicbrainz");
        if (it != j.end() && it->is_array()) {
            for (const auto& e : *it) r.musicbrainz.push_back(parse_musicbrainz(e));
        }
    }

    r.extras = extract_extras(j, {
        "timecode", "audio_id", "artist", "title", "album", "release_date",
        "label", "song_link", "isrc", "upc",
        "apple_music", "spotify", "deezer", "napster", "musicbrainz",
    });
    r.raw_response = j.dump();
    return r;
}

EnterpriseChunkResult parse_enterprise_chunk(const nlohmann::json& j) {
    EnterpriseChunkResult c;
    c.offset = j_str(j, "offset");
    if (j.is_object()) {
        auto it = j.find("songs");
        if (it != j.end() && it->is_array()) {
            for (const auto& song : *it) {
                EnterpriseMatch m;
                m.score        = j_int(song, "score");
                m.timecode     = j_str(song, "timecode");
                m.artist       = j_str(song, "artist");
                m.title        = j_str(song, "title");
                m.album        = j_str(song, "album");
                m.release_date = j_str(song, "release_date");
                m.label        = j_str(song, "label");
                m.isrc         = j_str(song, "isrc");
                m.upc          = j_str(song, "upc");
                m.song_link    = j_str(song, "song_link");
                m.start_offset = j_int(song, "start_offset");
                m.end_offset   = j_int(song, "end_offset");
                m.extras = extract_extras(song, {
                    "score", "timecode", "artist", "title", "album",
                    "release_date", "label", "isrc", "upc", "song_link",
                    "start_offset", "end_offset",
                });
                m.raw_response = song.dump();
                c.songs.push_back(std::move(m));
            }
        }
    }
    c.extras = extract_extras(j, {"songs", "offset"});
    c.raw_response = j.dump();
    return c;
}

LyricsResult parse_lyrics(const nlohmann::json& j) {
    LyricsResult r;
    r.artist     = j_str(j, "artist");
    r.title      = j_str(j, "title");
    r.lyrics     = j_str(j, "lyrics");
    r.song_id    = j_int(j, "song_id");
    r.media      = j_str(j, "media");
    r.full_title = j_str(j, "full_title");
    r.artist_id  = j_int(j, "artist_id");
    r.song_link  = j_str(j, "song_link");
    r.extras = extract_extras(j, {
        "artist", "title", "lyrics", "song_id", "media",
        "full_title", "artist_id", "song_link",
    });
    r.raw_response = j.dump();
    return r;
}

Stream parse_stream(const nlohmann::json& j) {
    Stream s;
    s.radio_id          = j_int(j, "radio_id");
    s.url               = j_str(j, "url");
    s.stream_running    = j_bool(j, "stream_running");
    s.longpoll_category = j_str(j, "longpoll_category");
    s.extras = extract_extras(j, {"radio_id", "url", "stream_running", "longpoll_category"});
    s.raw_response = j.dump();
    return s;
}

StreamCallbackSong parse_stream_callback_song(const nlohmann::json& j) {
    StreamCallbackSong s;
    s.score        = j_int(j, "score");
    s.artist       = j_str(j, "artist");
    s.title        = j_str(j, "title");
    s.album        = j_str(j, "album");
    s.release_date = j_str(j, "release_date");
    s.label        = j_str(j, "label");
    s.song_link    = j_str(j, "song_link");
    s.isrc         = j_str(j, "isrc");
    s.upc          = j_str(j, "upc");

    if (j.is_object()) {
        auto it = j.find("apple_music");
        if (it != j.end() && it->is_object()) s.apple_music = parse_apple_music(*it);
        it = j.find("spotify");
        if (it != j.end() && it->is_object()) s.spotify = parse_spotify(*it);
        it = j.find("deezer");
        if (it != j.end() && it->is_object()) s.deezer = parse_deezer(*it);
        it = j.find("napster");
        if (it != j.end() && it->is_object()) s.napster = parse_napster(*it);
        it = j.find("musicbrainz");
        if (it != j.end() && it->is_array()) {
            for (const auto& e : *it) s.musicbrainz.push_back(parse_musicbrainz(e));
        }
    }

    s.extras = extract_extras(j, {
        "score", "artist", "title", "album", "release_date", "label",
        "song_link", "isrc", "upc",
        "apple_music", "spotify", "deezer", "napster", "musicbrainz",
    });
    return s;
}

StreamCallbackMatch parse_stream_callback_match(const nlohmann::json& result_obj,
                                                const std::string& full_body) {
    StreamCallbackMatch m;
    m.radio_id    = j_int64(result_obj, "radio_id");
    m.timestamp   = j_str(result_obj, "timestamp");
    m.play_length = j_int(result_obj, "play_length");

    std::vector<StreamCallbackSong> songs;
    if (result_obj.is_object()) {
        auto it = result_obj.find("results");
        if (it != result_obj.end() && it->is_array()) {
            for (const auto& s : *it) {
                songs.push_back(parse_stream_callback_song(s));
            }
        }
    }
    if (songs.empty()) {
        throw AudDSerializationError("callback result.results is empty", full_body);
    }
    m.song = std::move(songs.front());
    if (songs.size() > 1) {
        m.alternatives.assign(songs.begin() + 1, songs.end());
    }
    m.extras = extract_extras(result_obj, {
        "radio_id", "timestamp", "play_length", "results",
    });
    m.raw_response = full_body;
    return m;
}

StreamCallbackNotification parse_stream_callback_notification(
    const nlohmann::json& notification_obj,
    int outer_time,
    const std::string& full_body) {
    StreamCallbackNotification n;
    n.radio_id             = j_int(notification_obj, "radio_id");
    if (notification_obj.is_object()) {
        auto it = notification_obj.find("stream_running");
        if (it != notification_obj.end() && !it->is_null()) {
            try { n.stream_running = it->get<bool>(); } catch (...) {}
        }
    }
    n.notification_code    = j_int(notification_obj, "notification_code");
    n.notification_message = j_str(notification_obj, "notification_message");
    n.time = outer_time;
    n.extras = extract_extras(notification_obj, {
        "radio_id", "stream_running", "notification_code", "notification_message",
    });
    n.raw_response = full_body;
    return n;
}

std::string branded_message(const nlohmann::json& result) {
    if (!result.is_object()) return "";
    std::string artist = j_str(result, "artist");
    std::string title  = j_str(result, "title");
    if (artist.empty() && title.empty()) return "";
    if (!artist.empty() && !title.empty()) return artist + " — " + title;
    if (!artist.empty()) return artist;
    return title;
}

[[noreturn]] void raise_from_error_response(const nlohmann::json& body,
                                            int http_status,
                                            const std::string& request_id,
                                            bool custom_catalog_context) {
    int code = 0;
    std::string msg;
    std::string request_method;
    std::string branded;
    std::map<std::string, std::string> requested_params;
    std::string raw = body.is_null() ? "" : body.dump();

    if (body.is_object()) {
        auto err_it = body.find("error");
        if (err_it != body.end() && err_it->is_object()) {
            code = j_int(*err_it, "error_code");
            msg  = j_str(*err_it, "error_message");
        }
        auto params_it = body.find("request_params");
        if (params_it == body.end()) params_it = body.find("requested_params");
        if (params_it != body.end() && params_it->is_object()) {
            for (auto it = params_it->begin(); it != params_it->end(); ++it) {
                if (it->is_string()) requested_params[it.key()] = it->get<std::string>();
                else                  requested_params[it.key()] = it->dump();
            }
        }
        request_method = j_str(body, "request_api_method");
        auto result_it = body.find("result");
        if (result_it != body.end()) branded = branded_message(*result_it);
    }

    if (custom_catalog_context && (code == 904 || code == 905)) {
        AudDCustomCatalogAccessError e(code, msg, http_status, request_id);
        e.message          = msg;
        e.requested_params = std::move(requested_params);
        e.request_method   = std::move(request_method);
        e.branded_message  = std::move(branded);
        e.raw_response     = std::move(raw);
        throw e;
    }
    AudDApiError e(code, msg, http_status, request_id);
    e.requested_params = std::move(requested_params);
    e.request_method   = std::move(request_method);
    e.branded_message  = std::move(branded);
    e.raw_response     = std::move(raw);
    throw e;
}

} // namespace audd::internal

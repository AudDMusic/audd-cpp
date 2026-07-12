// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#include "internal/json_parse.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <audd/error.hpp>

namespace audd::internal {

namespace {

// --- scalar coercion --------------------------------------------------------
//
// The AudD API is loosely typed: a field that is normally an int can arrive as
// a numeric string ("85"), a field that is normally a string can arrive as a
// number (123), and so on. The readers below coerce a wrong-typed *scalar*
// value to the expected type when it is convertible, and fall back to a caller-
// supplied default only when it is not (non-numeric string, object/array where
// a scalar was expected, etc.). Well-typed values take the fast path.
//
// Coercion policy (mirrors the wider SDK family's forward-compat model):
//   string  <- number rendered without a trailing ".0" for integers; bool as
//              "true"/"false"; object/array -> default.
//   int     <- double truncated toward zero; full-string-validated numeric
//              string; bool -> 0/1; else default.
//   double  <- int; full-string-validated numeric string; else default.
//   bool    <- number != 0; string via a strict case-insensitive, trimmed
//              whitelist ("true"/"1"/"yes"/"on" -> true;
//              "false"/"0"/"no"/"off"/"" -> false); any other string ->
//              default; else default.
//
// Numeric-string parsing is full-string strict after trimming and rejects
// non-decimal forms (nan, inf, 0x hex) so a stray "NaN" degrades to the
// default rather than a surprise value.

// trim_ascii returns `s` without leading/trailing ASCII whitespace.
std::string trim_ascii(const std::string& s) {
    std::size_t b = 0, e = s.size();
    auto is_ws = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
               c == '\f' || c == '\v';
    };
    while (b < e && is_ws(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && is_ws(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// parse_full_int parses `s` as a base-10 integer, requiring the entire trimmed
// string to be consumed (no "12abc" partial parses, no hex, no nan/inf).
// std::from_chars is locale-independent and base-10-only, so it rejects
// "0x1A" and non-numeric junk naturally. Accepts a leading '+' (stripped
// before from_chars, which itself accepts only '-') and surrounding
// whitespace. Returns nullopt on failure.
std::optional<std::int64_t> parse_full_int(const std::string& s) {
    std::string t = trim_ascii(s);
    if (!t.empty() && t.front() == '+') t.erase(t.begin());
    if (t.empty()) return std::nullopt;
    std::int64_t v = 0;
    const char* first = t.data();
    const char* last  = t.data() + t.size();
    auto res = std::from_chars(first, last, v, 10);
    if (res.ec != std::errc{} || res.ptr != last) return std::nullopt;
    return v;
}

// parse_full_double parses `s` as a decimal double, requiring the entire
// trimmed string to be consumed and rejecting nan/inf and hex forms. std::stod
// accepts "nan", "inf", and "0x..." on many platforms, so guard against those
// explicitly. Accepts a leading +/- and surrounding whitespace. Returns
// nullopt on failure.
std::optional<double> parse_full_double(const std::string& s) {
    const std::string t = trim_ascii(s);
    if (t.empty()) return std::nullopt;
    // Reject hex ("0x1A": 'x') and alphabetic nan/inf spellings up front by
    // rejecting any letter other than a decimal exponent 'e'/'E'; std::stod
    // would otherwise accept them on many platforms.
    for (char c : t) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalpha(uc) && c != 'e' && c != 'E') return std::nullopt;
    }
    try {
        std::size_t consumed = 0;
        double v = std::stod(t, &consumed);
        if (consumed != t.size()) return std::nullopt; // trailing junk
        if (!std::isfinite(v)) return std::nullopt;     // nan/inf guard
        return v;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// num_to_string renders a JSON number as a string: integers without a decimal
// point ("85"), doubles naturally ("8.5").
std::string num_to_string(const nlohmann::json& v) {
    if (v.is_number_integer())  return std::to_string(v.get<std::int64_t>());
    if (v.is_number_unsigned()) return std::to_string(v.get<std::uint64_t>());
    // Double. Integer-valued doubles render without a trailing ".0" ("85.0"
    // -> "85"); everything else uses nlohmann's shortest round-trippable form.
    double d = v.get<double>();
    if (std::isfinite(d) && d == std::trunc(d) &&
        d >= -9.2233720368547758e18 && d < 9.2233720368547758e18) {
        return std::to_string(static_cast<std::int64_t>(d));
    }
    return v.dump();
}

} // anonymous (parse primitives)

// --- public coercion entry points -------------------------------------------
// Exposed via json_parse.hpp so the readers below and the test suite can
// exercise the coercion policy directly.

std::optional<std::string> coerce_string(const nlohmann::json& v) {
    if (v.is_string())  return v.get<std::string>();
    if (v.is_boolean()) return v.get<bool>() ? std::string("true")
                                             : std::string("false");
    if (v.is_number())  return num_to_string(v);
    return std::nullopt; // object / array / null
}

std::optional<std::int64_t> coerce_int(const nlohmann::json& v) {
    if (v.is_number_integer())  return v.get<std::int64_t>();
    if (v.is_number_unsigned()) return static_cast<std::int64_t>(v.get<std::uint64_t>());
    if (v.is_number_float())    return static_cast<std::int64_t>(v.get<double>()); // trunc toward 0
    if (v.is_boolean())         return v.get<bool>() ? 1 : 0;
    if (v.is_string())          return parse_full_int(v.get<std::string>());
    return std::nullopt; // object / array / null
}

std::optional<double> coerce_double(const nlohmann::json& v) {
    if (v.is_number())  return v.get<double>();
    if (v.is_boolean()) return v.get<bool>() ? 1.0 : 0.0;
    if (v.is_string())  return parse_full_double(v.get<std::string>());
    return std::nullopt; // object / array / null
}

std::optional<bool> coerce_bool(const nlohmann::json& v) {
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number())  return v.get<double>() != 0.0;
    if (v.is_string()) {
        std::string s = trim_ascii(v.get<std::string>());
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (s.empty() || s == "false" || s == "0" || s == "no" || s == "off")
            return false;
        if (s == "true" || s == "1" || s == "yes" || s == "on")
            return true;
        return std::nullopt; // unrecognized -> not convertible
    }
    return std::nullopt; // object / array / null
}

namespace {

// find_value returns a pointer to the non-null value at `key`, or nullptr on a
// non-object, a missing key, or an explicit JSON null.
const nlohmann::json* find_value(const nlohmann::json& j, const std::string& key) {
    if (!j.is_object()) return nullptr;
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return nullptr;
    return &*it;
}

// j_str pulls a string field, coercing convertible scalars, returning "" on
// missing / null / not-convertible.
std::string j_str(const nlohmann::json& j, const std::string& key) {
    const nlohmann::json* v = find_value(j, key);
    if (!v) return "";
    if (auto s = coerce_string(*v)) return *s;
    return "";
}

int j_int(const nlohmann::json& j, const std::string& key) {
    const nlohmann::json* v = find_value(j, key);
    if (!v) return 0;
    if (auto n = coerce_int(*v)) return static_cast<int>(*n);
    return 0;
}

bool j_bool(const nlohmann::json& j, const std::string& key) {
    const nlohmann::json* v = find_value(j, key);
    if (!v) return false;
    if (auto b = coerce_bool(*v)) return *b;
    return false;
}

std::int64_t j_int64(const nlohmann::json& j, const std::string& key) {
    const nlohmann::json* v = find_value(j, key);
    if (!v) return 0;
    if (auto n = coerce_int(*v)) return *n;
    return 0;
}

} // anonymous

std::optional<double> offset_to_seconds(const std::string& offset) {
    if (offset.empty()) return std::nullopt;

    // Split on ':' into up to three colon-separated components.
    std::vector<std::string> parts;
    std::string cur;
    for (char ch : offset) {
        if (ch == ':') {
            parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    parts.push_back(cur);

    if (parts.empty() || parts.size() > 3) return std::nullopt;

    auto parse_component = [](const std::string& s) -> std::optional<double> {
        if (s.empty()) return std::nullopt;
        try {
            std::size_t consumed = 0;
            double v = std::stod(s, &consumed);
            if (consumed != s.size()) return std::nullopt; // trailing junk
            return v;
        } catch (const std::exception&) {
            return std::nullopt;
        }
    };

    double total = 0.0;
    for (const auto& p : parts) {
        auto v = parse_component(p);
        if (!v) return std::nullopt;
        total = total * 60.0 + *v;
    }
    return total;
}

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
    if (const nlohmann::json* v = find_value(j, "audio_id")) {
        if (auto n = coerce_int(*v)) r.audio_id = static_cast<int>(*n);
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
    if (const nlohmann::json* v = find_value(notification_obj, "stream_running")) {
        if (auto b = coerce_bool(*v)) n.stream_running = *b;
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

const nlohmann::json* result_array_or_null(const nlohmann::json& body) {
    auto it = body.find("result");
    if (it == body.end() || it->is_null() || !it->is_array()) return nullptr;
    return &*it;
}

LongpollDisposition classify_longpoll_response(int http_status,
                                               const nlohmann::json& body,
                                               bool body_empty) {
    if (http_status >= 400) return LongpollDisposition::Terminal;
    if (body_empty) return LongpollDisposition::Skip;
    // Keep-alive tick: an object carrying `timeout` and no event block.
    if (body.is_object() && body.contains("timeout") &&
        !body.contains("result") && !body.contains("notification")) {
        return LongpollDisposition::KeepAlive;
    }
    return LongpollDisposition::Event;
}

bool url_query_has_key(const std::string& url, const std::string& key) {
    auto q = url.find('?');
    if (q == std::string::npos) return false;
    const std::string query = url.substr(q + 1);
    std::size_t pos = 0;
    while (pos <= query.size()) {
        std::size_t amp = query.find('&', pos);
        std::string pair = query.substr(
            pos, amp == std::string::npos ? std::string::npos : amp - pos);
        std::size_t eq = pair.find('=');
        std::string k = (eq == std::string::npos) ? pair : pair.substr(0, eq);
        if (k == key) return true;
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return false;
}

} // namespace audd::internal

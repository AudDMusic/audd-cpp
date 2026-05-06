# audd-cpp

[![CI](https://github.com/AudDMusic/audd-cpp/actions/workflows/ci.yml/badge.svg)](https://github.com/AudDMusic/audd-cpp/actions/workflows/ci.yml)

Official C++ SDK for [music recognition API](https://audd.io): identify music from a short audio clip, a long audio file, or a live stream.

The API itself is so simple that it can easily be used even without an SDK: [docs.audd.io](https://docs.audd.io).

## Hello, AudD

```cmake
# CMakeLists.txt
find_package(audd CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE audd::audd)
```

Get your API token at [dashboard.audd.io](https://dashboard.audd.io).

Identify a song hosted at a URL:

```cpp
#include <iostream>
#include <audd/audd.hpp>

int main() {
    audd::AudD client("test"); // get a real token at dashboard.audd.io

    auto result = client.recognize("https://audd.tech/example.mp3");
    if (!result) {
        std::cout << "no match\n";
        return 0;
    }
    std::cout << result->artist << " - " << result->title << "\n";
}
```

Identify a song from a local file path:

```cpp
auto result = client.recognize(audd::SourceFilePath{"/path/to/clip.mp3"});
```

`recognize()` accepts a `std::string` (auto-classified as URL or file path), an explicit `audd::SourceUrl` / `audd::SourceFilePath`, or an `audd::SourceBytes` carrying raw audio bytes. For files longer than ~25 seconds, use `recognize_enterprise(source, opts)`, which returns `std::vector<EnterpriseMatch>` across the file's chunks. Each match carries the same core tags plus `score`, `start_offset`, `end_offset`, `isrc`, `upc`. Access to `isrc`, `upc`, and `score` requires a Startup plan or higher — [contact us](mailto:api@audd.io) for enterprise features.

Every blocking method has an `_async` twin returning `std::future`: `recognize_async`, `recognize_enterprise_async`, `streams().add_async`, `advanced().find_lyrics_async`, etc. Reach for the future-based form when you want non-blocking dispatch from a UI thread or want to fan out concurrent calls.

Requires a C++17 compiler and libcurl. The library is single-target (`audd::audd`); link it and `#include <audd/audd.hpp>`.

## Authentication

Pass your token to the constructor:

```cpp
audd::AudD client("your-api-token");
```

Or leave it empty and the SDK reads the `AUDD_API_TOKEN` environment variable:

```cpp
// AUDD_API_TOKEN=your-token ...
audd::AudD client("");
```

Get a real token at [dashboard.audd.io](https://dashboard.audd.io). The public `"test"` token works for the snippets above but is capped at 10 requests.

For long-running services that pull tokens from a secret manager and need to swap them without restarting:

```cpp
client.set_api_token(new_token);
```

In-flight requests continue with the previous token; subsequent ones use the new value.

If you'd rather fail fast at construction time when no token is configured, use `audd::AudD::strict("")`, which throws `audd::AuddMissingApiTokenError`.

## What you get back

By default `recognize()` returns the core tags plus AudD's universal song link — no metadata-block opt-in needed:

```cpp
auto result = client.recognize("https://audd.tech/example.mp3");
if (!result) return; // no match

std::cout << result->artist << " - " << result->title << "\n";
std::cout << "Album:     " << result->album << "\n";
std::cout << "Released:  " << result->release_date << "\n";
std::cout << "Label:     " << result->label << "\n";
std::cout << "AudD song: " << result->song_link << "\n"; // links into every provider

// Helpers driven off song_link — work without any return-metadata opt-in:
std::cout << "Cover art:  " << result->thumbnail_url() << "\n";
std::cout << "On Spotify: " << result->streaming_url(audd::StreamingProvider::Spotify) << "\n";
for (const auto& [provider, url] : result->streaming_urls()) {
    std::cout << "  " << audd::streaming_provider_name(provider) << " -> " << url << "\n";
}
```

If you need provider-specific metadata blocks, opt in per call. Request only what you need — each provider you ask for adds latency:

```cpp
audd::RecognizeOptions opts;
opts.return_metadata = {"apple_music", "spotify"};
auto result = client.recognize("https://audd.tech/example.mp3", opts);

if (result->apple_music) std::cout << "Apple Music: " << result->apple_music->url << "\n";
if (result->spotify)     std::cout << "Spotify URI: " << result->spotify->uri << "\n";
std::cout << "Preview: " << result->preview_url() << "\n"; // first across requested providers; "" if none
```

Valid `return_metadata` values: `apple_music`, `spotify`, `deezer`, `napster`, `musicbrainz`. The metadata-block fields are `std::optional<...>`; `musicbrainz` is a `std::vector<...>`.

### Reading additional metadata

`result->extras` is a `std::map<std::string, nlohmann::json>` of every server field outside the typed surface; `result->raw_response` is the original JSON string. Use them to read undocumented metadata or beta fields not yet exposed as typed properties:

```cpp
if (auto it = result->extras.find("song_length"); it != result->extras.end()) {
    int seconds = it->second.get<int>();
    std::cout << "song length: " << seconds << "\n";
}

// Same channel exists on every metadata block:
if (result->apple_music) {
    if (auto it = result->apple_music->extras.find("genreNames"); it != result->apple_music->extras.end()) {
        auto genres = it->second.get<std::vector<std::string>>();
    }
}
```

## Errors

Every error from this SDK derives from `audd::AuddError`. Discriminate by category:

```cpp
try {
    auto result = client.recognize(source);
    // ...
} catch (const audd::AuddError& e) {
    switch (e.category()) {
        case audd::ErrorCategory::Authentication:
            // 900 / 901 / 903 — token problems
            break;
        case audd::ErrorCategory::Quota:
            // 902 — quota exceeded
            break;
        case audd::ErrorCategory::InvalidAudio:
            // 300 / 400 / 500 — audio is the problem
            break;
        case audd::ErrorCategory::RateLimit:
            // 611 — back off and retry
            break;
        case audd::ErrorCategory::Server:
            // 5xx, non-JSON gateway responses
            break;
        default:
            std::cerr << "audd: " << e.what() << "\n";
    }
}
```

For the underlying numeric code, message, and request ID, catch the typed error:

```cpp
try { ... }
catch (const audd::AuddApiError& e) {
    std::cerr << "[#" << e.error_code << "] " << e.message
              << " (request_id=" << e.request_id << ")\n";
}
```

Categories: `Authentication`, `Quota`, `Subscription`, `CustomCatalogAccess`, `InvalidRequest`, `InvalidAudio`, `RateLimit`, `StreamLimit`, `NotReleased`, `Blocked`, `NeedsUpdate`, `Server`, `Connection`, `Serialization`.

## Configuration

```cpp
audd::ClientConfig cfg;
cfg.max_attempts        = 5;
cfg.backoff_factor      = std::chrono::seconds(1);
cfg.standard_timeout    = std::chrono::minutes(2);
cfg.enterprise_timeout  = std::chrono::hours(2);
cfg.on_event = [](const audd::AuddEvent& e) {
    // see Observability
};
cfg.on_deprecation = [](const std::string& msg) {
    // server-side soft-deprecation warnings (code 51) land here
    std::cerr << "audd-deprecation: " << msg << "\n";
};
audd::AudD client("your-api-token", cfg);
```

Retries are conservative: connection-level failures (DNS, TCP, TLS) are retried up to `max_attempts` with exponential backoff. Post-upload errors are not retried — the server may have already done the metered work.

## Observability

`cfg.on_event` receives a structured `AuddEvent` for every `Request` / `Response` / `Exception` on the wire — method/URL/status/elapsed/request_id, no api_token, no body bytes:

```cpp
cfg.on_event = [](const audd::AuddEvent& e) {
    std::cout << "audd "
              << (e.kind == audd::AuddEvent::Kind::Request ? "req" :
                  e.kind == audd::AuddEvent::Kind::Response ? "rsp" : "exc")
              << " " << e.method << " " << e.http_status
              << " " << e.elapsed.count() << "ms "
              << "req_id=" << e.request_id << "\n";
};
```

Hook errors are swallowed by the SDK — observability never breaks the request path.

## Streams

Stream recognition turns AudD into a continuous monitor for an audio stream (internet radio, Twitch, YouTube live, raw HLS/Icecast) and notifies you for every recognized song. Set up streams once, then either receive matches via a callback URL or poll for them.

```cpp
auto& streams = client.streams();

// 1. Tell AudD where to POST recognition results for your account.
audd::SetCallbackUrlOptions opts;
opts.return_metadata = {"apple_music", "spotify"};
streams.set_callback_url("https://your.app/audd/callback", opts);

// 2. Add streams to monitor.
streams.add({.url = "https://example.com/radio.m3u8", .radio_id = 1});
streams.add({.url = "twitch:somechannel",            .radio_id = 2});

// 3. Inspect what you have configured.
for (const auto& s : streams.list()) {
    std::cout << s.radio_id << " " << s.url << " running=" << s.stream_running << "\n";
}
```

Inside your callback receiver, parse the POST body into a typed event:

```cpp
auto event = audd::handle_callback(request_body);

std::visit([](auto&& v) {
    using T = std::decay_t<decltype(v)>;
    if constexpr (std::is_same_v<T, audd::StreamCallbackMatch>) {
        std::cout << "matched: " << v.song.artist << " - " << v.song.title << "\n";
        for (const auto& alt : v.alternatives) {
            std::cout << "  alt: " << alt.artist << " - " << alt.title << "\n";
        }
    } else if constexpr (std::is_same_v<T, audd::StreamCallbackNotification>) {
        std::cout << "notification: " << v.notification_message << "\n";
    }
}, event);
```

`handle_callback(body)` and `parse_callback(body)` accept either `std::string` or raw bytes. The result is a `std::variant<StreamCallbackMatch, StreamCallbackNotification>`; every event reaches the right `if constexpr` branch.

See [`examples/streams_callback_handler.cpp`](./examples/streams_callback_handler.cpp) (using cpp-httplib) and [`examples/streams_setup.cpp`](./examples/streams_setup.cpp) for runnable code.

### Receiving events without a callback URL (longpoll)

If hosting a callback receiver isn't an option, longpoll for events from the client side. The poll exposes three asynchronous streams — matches, notifications, and a single terminal error — consumable in three ways.

**Callback-style** (blocks the calling thread until terminal):

```cpp
auto category = client.streams().derive_longpoll_category(/*radio_id*/ 1);
auto poll = client.streams().longpoll(category);

poll.run(
    [](audd::StreamCallbackMatch m) {
        std::cout << "matched: " << m.song.artist << " - " << m.song.title << "\n";
    },
    [](audd::StreamCallbackNotification n) {
        std::cout << "notification: " << n.notification_message << "\n";
    },
    [](std::exception_ptr ep) {
        try { std::rethrow_exception(ep); }
        catch (const std::exception& e) { std::cerr << "longpoll: " << e.what(); }
    });
```

**Blocking pull** (one event at a time, your own loop):

```cpp
auto poll = client.streams().longpoll(category);
while (auto m = poll.next_match()) {
    std::cout << m->song.artist << " - " << m->song.title << "\n";
}
```

**Future-based** (non-blocking dispatch):

```cpp
auto fut = poll.next_match_async();
// ... do other work ...
auto m = fut.get(); // std::optional<StreamCallbackMatch>; nullopt = stream closed
```

`audd::derive_longpoll_category(token, radio_id)` is also available as a free function — useful for computing categories on a server and shipping them to a frontend without leaking the api_token.

## Custom catalog (advanced)

> [!WARNING]
> The custom-catalog endpoint is **not** music recognition. It adds songs to your account's **private fingerprint database**, so AudD's recognition can later identify *your own* tracks for *your account only*. If you intended to identify music, use `client.recognize(...)` (or `client.recognize_enterprise(...)` for files longer than 25 seconds) instead.

```cpp
client.custom_catalog().add(audio_id, audd::SourceFilePath{"/path/to/track.mp3"});
```

Custom-catalog access requires a separate subscription. Contact api@audd.io to get it enabled. Subscription failures throw `audd::AuddCustomCatalogAccessError`, which carries a long, friendlier message than the generic `Subscription` category.

## Advanced

`client.advanced()` exposes the typed `find_lyrics(query)` method plus a generic raw-method escape hatch for AudD endpoints not yet wrapped on this SDK:

```cpp
auto body = client.advanced().raw_request("newMethodName", {{"param", "value"}});
// body is a nlohmann::json
```

## License & support

MIT — see [LICENSE](./LICENSE). Security policy: [SECURITY.md](./SECURITY.md).

Bug reports and PRs welcome. For account / API questions, email api@audd.io.

## Vendored dependencies

This SDK vendors three single-header libraries to keep build setup minimal:

- `vendor/nlohmann/json.hpp` — [nlohmann/json](https://github.com/nlohmann/json), MIT
- `vendor/cpp-httplib/httplib.h` — [yhirose/cpp-httplib](https://github.com/yhirose/cpp-httplib), MIT (used by examples)
- `vendor/doctest/doctest.h` — [doctest/doctest](https://github.com/doctest/doctest), MIT (used by tests)

libcurl is the one external dependency (used outbound). Ubuntu: `apt install libcurl4-openssl-dev`. macOS: `brew install curl`.

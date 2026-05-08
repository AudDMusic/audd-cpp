// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#ifndef AUDD_LONGPOLL_HPP
#define AUDD_LONGPOLL_HPP

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>

#include <audd/callback.hpp>
#include <audd/error.hpp>

namespace audd {

// LongpollOptions controls the longpoll iterator. Default values use sane
// defaults.
struct LongpollOptions {
    // since_time is the unix timestamp to resume from. 0 means "start from now".
    int since_time = 0;
    // timeout_seconds is the longpoll timeout in seconds (server-side default: 50).
    int timeout_seconds = 50;
    // skip_callback_check disables the preflight that detects the
    // "no callback URL" misconfiguration. Default false (preflight is on).
    bool skip_callback_check = false;
};

// derive_longpoll_category computes the 9-char longpoll category for a token
// + radio_id. Pure function — no network call.
//
// Formula (per docs.audd.io/streams.md): hex-MD5 of (hex-MD5 of api_token,
// concatenated with the radio_id rendered as a decimal string), truncated to
// the first 9 hex chars.
//
// Use this to share categories with browser/widget code without exposing the
// api_token.
std::string derive_longpoll_category(const std::string& api_token, int radio_id);

// LongpollPoll is an active long-poll subscription with three asynchronous
// streams of values: matches, notifications, and a single terminal error.
//
// Three consumption patterns are supported:
//
//   1. Blocking pull: next_match() / next_notification() / next_error() return
//      std::optional<...>; nullopt means the stream has terminated.
//
//   2. Future-based: try_next_match_async(), try_next_notification_async(),
//      try_next_error_async(). Each returns a std::future<std::optional<...>>
//      that resolves when a value (or terminal nullopt) is available.
//
//   3. Callback-driven: run(on_match, on_notification, on_error) blocks until
//      a terminal error fires, calling the registered callbacks per event.
//
// The poll runs a background thread that drives HTTP I/O. Closing the poll
// (or letting it go out of scope) joins the thread cleanly.
class LongpollPoll {
public:
    LongpollPoll();
    ~LongpollPoll();
    LongpollPoll(const LongpollPoll&) = delete;
    LongpollPoll& operator=(const LongpollPoll&) = delete;
    LongpollPoll(LongpollPoll&&) noexcept;
    LongpollPoll& operator=(LongpollPoll&&) noexcept;

    // Blocking pulls. Each returns std::optional<T> — nullopt when the
    // stream is closed (terminal error fired or poll was closed).
    std::optional<StreamCallbackMatch>        next_match();
    std::optional<StreamCallbackNotification> next_notification();
    std::optional<std::exception_ptr>         next_error();

    // Async pulls. Each returns a std::future that resolves when a value
    // (or terminal nullopt) is available. The future is satisfied on the
    // poll's worker thread; callers should not block on it from that
    // thread.
    std::future<std::optional<StreamCallbackMatch>>        next_match_async();
    std::future<std::optional<StreamCallbackNotification>> next_notification_async();
    std::future<std::optional<std::exception_ptr>>         next_error_async();

    // Callback-style consumption. Blocks the calling thread until a
    // terminal error fires or close() is called. Any callback may be empty;
    // unmatched events are silently dropped.
    using OnMatch        = std::function<void(StreamCallbackMatch)>;
    using OnNotification = std::function<void(StreamCallbackNotification)>;
    using OnError        = std::function<void(std::exception_ptr)>;
    void run(OnMatch on_match,
             OnNotification on_notification = {},
             OnError on_error = {});

    // close stops the background poll. Idempotent.
    void close() noexcept;

    // Implementation detail; for use by the streams sub-client.
    struct Impl;
    explicit LongpollPoll(std::unique_ptr<Impl> impl);
    Impl* impl() noexcept { return impl_.get(); }

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace audd

#endif // AUDD_LONGPOLL_HPP

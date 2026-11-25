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

// LongpollOptions controls a longpoll subscription.
struct LongpollOptions {
    // since_time is a server timestamp in milliseconds since the Unix epoch,
    // as carried by an earlier longpoll response: only events newer than it
    // are delivered. 0 means "start from now".
    long long since_time = 0;
    // timeout_seconds is how long the server holds each request open while
    // waiting for events: 1 to 120. 0 or less means 50. A value the server
    // rejects ends the poll with an AudDInvalidRequestError.
    int timeout_seconds = 50;
    // skip_callback_check disables the preflight that detects the
    // "no callback URL" misconfiguration. Default false (preflight is on).
    // A client with no api_token never runs the preflight.
    bool skip_callback_check = false;
    // on_transient_error, when set, is called for each failed request the
    // poll retries (a network failure, or HTTP 408, 429 or 5xx), with the
    // error that caused the retry: an AudDConnectionError or an AudDApiError.
    // The poll keeps running. It is called on the poll's background thread;
    // calling close() from it is fine. Exceptions it throws are ignored.
    std::function<void(std::exception_ptr)> on_transient_error;
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
//   2. Future-based: next_match_async(), next_notification_async(),
//      next_error_async(). Each returns a std::future<std::optional<...>>
//      that resolves when a value (or terminal nullopt) is available.
//
//   3. Callback-driven: run(on_match, on_notification, on_error) blocks until
//      a terminal error fires, calling the registered callbacks per event.
//
// The poll runs a background thread that drives HTTP I/O:
//
//   - Events are delivered in the order the server sent them, and each
//     request asks only for events newer than the newest one seen (or than a
//     timeout response's timestamp). An event that can't be parsed, or a
//     recognition with no songs, is skipped.
//   - When the server rejects the request (for example a timeout_seconds
//     outside 1-120), the poll ends with an AudDInvalidRequestError carrying
//     the server's text. Other HTTP 4xx responses end it with an AudDApiError.
//   - Network failures and HTTP 408, 429 and 5xx are retried, and so is a
//     response with nothing new in it (an unknown shape, an empty or non-JSON
//     body), after a pause that starts at 1 second and doubles up to 30
//     seconds. These never end the poll; LongpollOptions::on_transient_error
//     reports the failures.
//   - The requests carry no api_token: the category authorizes them.
//
// Closing the poll (or letting it go out of scope) stops a pause or a
// waiting request within about a second and joins the thread.
//
// The worker borrows the AudD client that created it: keep that client alive
// until the poll is stopped (close() returns or the LongpollPoll is
// destroyed). Destroying the client while a poll is still active is undefined
// behavior.
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
    // terminal error fires or close() is called. The callbacks run on the
    // calling thread, one at a time, in the order the events arrived;
    // on_error runs at most once, last, and then the poll is closed. Any
    // callback may be empty; unmatched events are silently dropped. An
    // exception thrown by a callback propagates out of run() and leaves the
    // poll open.
    using OnMatch        = std::function<void(StreamCallbackMatch)>;
    using OnNotification = std::function<void(StreamCallbackNotification)>;
    using OnError        = std::function<void(std::exception_ptr)>;
    void run(OnMatch on_match,
             OnNotification on_notification = {},
             OnError on_error = {});

    // close stops the background poll, including a pause or a request that
    // is waiting for the server. Idempotent; safe to call from any thread,
    // including from a callback. Values already queued can still be pulled.
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

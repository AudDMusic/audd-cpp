// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)
//
// Longpoll worker internals. Not part of the public API; exposed in this
// header so the test suite can drive the worker against a local server.

#ifndef AUDD_INTERNAL_LONGPOLL_INTERNAL_HPP
#define AUDD_INTERNAL_LONGPOLL_INTERNAL_HPP

#include <chrono>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <audd/callback.hpp>
#include <audd/longpoll.hpp>

#include "internal/http_client.hpp"

namespace audd::internal {

// LongpollPage is what one longpoll response yields.
struct LongpollPage {
    // events are the deliverable events, in the order the server sent them.
    std::vector<CallbackEvent> events;
    // since is the since_time for the next request, when the response
    // carried one: the newest event timestamp, or a timeout response's
    // timestamp.
    std::optional<long long> since;
    // events_shape is set for an {"events": [...]} response. Such a response
    // moves the cursor only through its timestamps.
    bool events_shape = false;
    // error is set when the server rejected the request; polling stops.
    std::exception_ptr error;
};

// parse_longpoll_response interprets one longpoll response body. The server
// answers with one of:
//
//   {"events":[{"timestamp":<ms>,"category":"...","data":<callback body>}]}
//       Each `data` (an object, or a JSON string holding one) is parsed with
//       parse_callback. An event whose data doesn't parse, or whose
//       recognition has an empty `results` list, is skipped. The newest
//       timestamp is the next since_time.
//   {"timeout":"no events before timeout","timestamp":<ms>}
//       Nothing new; the timestamp is the next since_time.
//   {"error":"Invalid timeout arg.  Must be 1-120."}
//       The request is invalid and sending it again gets the same answer:
//       `error` holds an AudDInvalidRequestError carrying the server's text
//       (an AudD error object, {"error":{"error_code":...}}, maps by its
//       code like any API error).
//
// A callback body at the top level ({"result":...} or {"notification":...},
// with an optional top-level timestamp) is delivered as one event. Anything
// else (non-JSON, empty, an unknown shape) yields an empty page. Never
// throws.
LongpollPage parse_longpoll_response(const HttpResponse& resp);

// The pause before the next request after a response with nothing new in it,
// or after a failed request, starts at kLongpollBackoffMin and doubles up to
// kLongpollBackoffMax.
inline constexpr std::chrono::milliseconds kLongpollBackoffMin{1000};
inline constexpr std::chrono::milliseconds kLongpollBackoffMax{30000};

// longpoll_backoff_delay returns the pause after the n-th consecutive
// unproductive request (n counts from 0): `min` doubled n times, capped at
// `max`.
std::chrono::milliseconds longpoll_backoff_delay(int n,
                                                 std::chrono::milliseconds min,
                                                 std::chrono::milliseconds max);

// LongpollWorkerConfig is everything one longpoll subscription needs.
struct LongpollWorkerConfig {
    HttpClient*               http = nullptr; // borrowed; must outlive the poll
    std::string               url;            // the longpoll endpoint
    std::string               category;
    long long                 since_time = 0;
    int                       timeout_seconds = 50;
    // request_timeout bounds each HTTP request; sized above timeout_seconds
    // so a held request isn't cut short.
    std::chrono::milliseconds request_timeout{0};
    std::chrono::milliseconds backoff_min = kLongpollBackoffMin;
    std::chrono::milliseconds backoff_max = kLongpollBackoffMax;
    std::function<void(std::exception_ptr)> on_transient_error;
};

// start_longpoll_worker starts the background thread for one subscription
// and returns its handle. The requests carry no api_token.
LongpollPoll start_longpoll_worker(LongpollWorkerConfig cfg);

} // namespace audd::internal

#endif // AUDD_INTERNAL_LONGPOLL_INTERNAL_HPP

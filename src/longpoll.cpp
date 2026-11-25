// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#include <audd/longpoll.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include <audd/callback.hpp>
#include <audd/error.hpp>

#include "internal/http_client.hpp"
#include "internal/json_parse.hpp"
#include "internal/longpoll_internal.hpp"
#include "internal/md5.hpp"

namespace audd {

std::string derive_longpoll_category(const std::string& api_token, int radio_id) {
    std::string inner = audd::internal::md5_hex(api_token);
    std::string outer = audd::internal::md5_hex(inner + std::to_string(radio_id));
    return outer.substr(0, 9);
}

// Shared state between the worker thread and the consumer. One mutex and one
// condition variable cover all three queues, so run() can wait for whichever
// value arrives first and dispatch them in arrival order (each value carries
// a sequence number). Once `ended` is set nothing more is queued; consumers
// drain what is there and then receive nullopt.
struct LongpollPoll::Impl {
    template <typename T>
    using Queue = std::deque<std::pair<std::uint64_t, T>>;

    std::mutex                          m;
    std::condition_variable             cv;
    Queue<StreamCallbackMatch>          matches;
    Queue<StreamCallbackNotification>   notifications;
    Queue<std::exception_ptr>           errors;
    std::uint64_t                       next_seq = 0;
    bool                                ended = false;

    // Stop request. Read by the worker loop and by its in-flight HTTP
    // request, which aborts once it is set.
    std::atomic<bool> closed{false};
    std::thread       worker;

    template <typename T>
    void push(Queue<T>& q, T v) {
        {
            std::lock_guard<std::mutex> lk(m);
            if (ended) return;
            q.emplace_back(next_seq++, std::move(v));
        }
        cv.notify_all();
    }

    template <typename T>
    std::optional<T> pull(Queue<T>& q) {
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&] { return !q.empty() || ended; });
        if (q.empty()) return std::nullopt;
        T v = std::move(q.front().second);
        q.pop_front();
        return v;
    }

    // end marks that nothing more will be queued.
    void end() {
        {
            std::lock_guard<std::mutex> lk(m);
            ended = true;
        }
        cv.notify_all();
    }

    void close() {
        closed.store(true);
        end();
    }

    // pause waits `d`, or less if the poll is closed meanwhile. Returns false
    // when the poll was closed.
    bool pause(std::chrono::milliseconds d) {
        std::unique_lock<std::mutex> lk(m);
        return !cv.wait_for(lk, d, [&] { return closed.load(); });
    }

    ~Impl() {
        close();
        if (worker.joinable()) worker.join();
    }
};

LongpollPoll::LongpollPoll() = default;
LongpollPoll::LongpollPoll(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
LongpollPoll::~LongpollPoll() {
    if (impl_) impl_->close();
}
LongpollPoll::LongpollPoll(LongpollPoll&&) noexcept = default;
LongpollPoll& LongpollPoll::operator=(LongpollPoll&&) noexcept = default;

std::optional<StreamCallbackMatch> LongpollPoll::next_match() {
    if (!impl_) return std::nullopt;
    return impl_->pull(impl_->matches);
}

std::optional<StreamCallbackNotification> LongpollPoll::next_notification() {
    if (!impl_) return std::nullopt;
    return impl_->pull(impl_->notifications);
}

std::optional<std::exception_ptr> LongpollPoll::next_error() {
    if (!impl_) return std::nullopt;
    return impl_->pull(impl_->errors);
}

std::future<std::optional<StreamCallbackMatch>> LongpollPoll::next_match_async() {
    return std::async(std::launch::async, [this]() { return this->next_match(); });
}

std::future<std::optional<StreamCallbackNotification>>
LongpollPoll::next_notification_async() {
    return std::async(std::launch::async, [this]() { return this->next_notification(); });
}

std::future<std::optional<std::exception_ptr>> LongpollPoll::next_error_async() {
    return std::async(std::launch::async, [this]() { return this->next_error(); });
}

void LongpollPoll::run(OnMatch on_match,
                       OnNotification on_notification,
                       OnError on_error) {
    if (!impl_) return;
    Impl& s = *impl_;
    for (;;) {
        std::optional<StreamCallbackMatch>        match;
        std::optional<StreamCallbackNotification> notification;
        std::optional<std::exception_ptr>         error;
        {
            std::unique_lock<std::mutex> lk(s.m);
            s.cv.wait(lk, [&] {
                return !s.matches.empty() || !s.notifications.empty() ||
                       !s.errors.empty() || s.ended;
            });
            // Take the oldest queued value.
            constexpr auto kNone = std::numeric_limits<std::uint64_t>::max();
            const std::uint64_t m_seq = s.matches.empty() ? kNone : s.matches.front().first;
            const std::uint64_t n_seq = s.notifications.empty() ? kNone : s.notifications.front().first;
            const std::uint64_t e_seq = s.errors.empty() ? kNone : s.errors.front().first;
            const std::uint64_t first = std::min({m_seq, n_seq, e_seq});
            if (first == kNone) return; // ended and drained
            if (first == m_seq) {
                match = std::move(s.matches.front().second);
                s.matches.pop_front();
            } else if (first == n_seq) {
                notification = std::move(s.notifications.front().second);
                s.notifications.pop_front();
            } else {
                error = s.errors.front().second;
                s.errors.pop_front();
            }
        }
        if (match) {
            if (on_match) on_match(std::move(*match));
        } else if (notification) {
            if (on_notification) on_notification(std::move(*notification));
        } else if (error) {
            if (on_error) on_error(*error);
            s.close();
            return;
        }
    }
}

void LongpollPoll::close() noexcept {
    if (impl_) impl_->close();
}

namespace internal {

namespace {

// timestamp_field reads a positive millisecond timestamp (a JSON number or a
// numeric string) from obj[key].
std::optional<long long> timestamp_field(const nlohmann::json& obj, const char* key) {
    if (!obj.is_object()) return std::nullopt;
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null() || it->is_boolean()) return std::nullopt;
    auto n = coerce_int(*it);
    if (!n || *n <= 0) return std::nullopt;
    return static_cast<long long>(*n);
}

// parse_event_text parses one callback body; nullopt when it doesn't parse
// (including a recognition whose `results` list is empty).
std::optional<CallbackEvent> parse_event_text(const std::string& text) {
    try {
        return parse_callback(text);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// parse_event_data parses an event's `data`, which is an object or a JSON
// string holding one.
std::optional<CallbackEvent> parse_event_data(const nlohmann::json& data) {
    if (data.is_string()) return parse_event_text(data.get<std::string>());
    if (data.is_object()) return parse_event_text(data.dump());
    return std::nullopt;
}

std::exception_ptr longpoll_error(const HttpResponse& resp, const nlohmann::json& err) {
    if (err.is_object()) {
        // An AudD error block: map it by its code like any API error.
        try {
            raise_from_error_response(resp.json_body, resp.http_status,
                                      resp.request_id, false);
        } catch (...) {
            return std::current_exception();
        }
    }
    std::string msg = coerce_string(err).value_or(err.dump());
    if (msg.find_first_not_of(" \t\r\n") == std::string::npos) {
        msg = "Longpoll request rejected";
    }
    AudDInvalidRequestError e(0, msg, resp.http_status, resp.request_id);
    e.raw_response = resp.raw_body;
    return std::make_exception_ptr(e);
}

AudDApiError http_status_error(const HttpResponse& resp) {
    AudDApiError e(0, "Longpoll endpoint returned HTTP " + std::to_string(resp.http_status),
                   resp.http_status, resp.request_id);
    e.raw_response = resp.raw_body;
    return e;
}

bool is_transient_status(int status) {
    return status == 408 || status == 429 || status >= 500;
}

void run_worker(LongpollPoll::Impl& s, const LongpollWorkerConfig& cfg) {
    long long since = cfg.since_time;
    int misses = 0; // consecutive requests that brought nothing new or failed

    auto wait_before_next = [&]() -> bool {
        auto d = longpoll_backoff_delay(misses, cfg.backoff_min, cfg.backoff_max);
        if (misses < 64) ++misses;
        return s.pause(d);
    };
    auto report_transient = [&](std::exception_ptr ep) {
        if (!cfg.on_transient_error) return;
        try { cfg.on_transient_error(ep); } catch (...) {}
    };

    GetOptions get_opts;
    get_opts.send_token = false;
    get_opts.timeout    = cfg.request_timeout;
    get_opts.abort      = &s.closed;

    while (!s.closed.load()) {
        std::map<std::string, std::string> params;
        params["category"] = cfg.category;
        params["timeout"]  = std::to_string(cfg.timeout_seconds);
        if (since > 0) params["since_time"] = std::to_string(since);

        HttpResponse resp;
        try {
            resp = cfg.http->get(cfg.url, params, get_opts);
        } catch (const AudDConnectionError&) {
            if (s.closed.load()) return;
            report_transient(std::current_exception());
            if (!wait_before_next()) return;
            continue;
        }
        if (s.closed.load()) return;

        if (is_transient_status(resp.http_status)) {
            report_transient(std::make_exception_ptr(http_status_error(resp)));
            if (!wait_before_next()) return;
            continue;
        }

        LongpollPage page = parse_longpoll_response(resp);
        if (!page.error && resp.http_status >= 400) {
            page.error = std::make_exception_ptr(http_status_error(resp));
        }
        if (page.error) {
            s.push(s.errors, page.error);
            return;
        }
        for (auto& ev : page.events) {
            if (auto* m = std::get_if<StreamCallbackMatch>(&ev)) {
                s.push(s.matches, std::move(*m));
            } else if (auto* n = std::get_if<StreamCallbackNotification>(&ev)) {
                s.push(s.notifications, std::move(*n));
            }
        }

        // An events response moves on only through its timestamps: without
        // a newer since_time the next request would return the same events.
        bool progressed = !page.events.empty() && !page.events_shape;
        if (page.since && *page.since != since) {
            since = *page.since;
            progressed = true;
        }
        if (progressed) {
            misses = 0;
            continue;
        }
        if (!wait_before_next()) return;
    }
}

} // anonymous

LongpollPage parse_longpoll_response(const HttpResponse& resp) {
    LongpollPage page;
    const nlohmann::json& body = resp.json_body;
    if (!body.is_object()) return page;

    auto err_it = body.find("error");
    if (err_it != body.end() && !err_it->is_null()) {
        page.error = longpoll_error(resp, *err_it);
        return page;
    }

    auto events_it = body.find("events");
    if (events_it != body.end()) {
        page.events_shape = true;
        if (!events_it->is_array()) return page;
        for (const auto& item : *events_it) {
            if (!item.is_object()) continue;
            if (auto ts = timestamp_field(item, "timestamp")) {
                if (!page.since || *ts > *page.since) page.since = ts;
            }
            auto data_it = item.find("data");
            if (data_it == item.end()) continue;
            if (auto ev = parse_event_data(*data_it)) page.events.push_back(std::move(*ev));
        }
        return page;
    }

    if (body.contains("result") || body.contains("notification")) {
        if (auto ev = parse_event_text(resp.raw_body)) page.events.push_back(std::move(*ev));
        page.since = timestamp_field(body, "timestamp");
        return page;
    }

    if (body.contains("timeout")) {
        page.since = timestamp_field(body, "timestamp");
    }
    return page;
}

std::chrono::milliseconds longpoll_backoff_delay(int n,
                                                 std::chrono::milliseconds min,
                                                 std::chrono::milliseconds max) {
    if (min.count() <= 0) min = std::chrono::milliseconds(1);
    if (max < min) max = min;
    auto d = min;
    for (int i = 0; i < n && d < max; ++i) d *= 2;
    return std::min(d, max);
}

LongpollPoll start_longpoll_worker(LongpollWorkerConfig cfg) {
    auto impl = std::make_unique<LongpollPoll::Impl>();
    LongpollPoll::Impl& s = *impl;
    if (cfg.timeout_seconds <= 0) cfg.timeout_seconds = 50;
    s.worker = std::thread([&s, cfg = std::move(cfg)]() {
        try {
            run_worker(s, cfg);
        } catch (...) {
            s.push(s.errors, std::current_exception());
        }
        s.end();
    });
    return LongpollPoll(std::move(impl));
}

} // namespace internal

} // namespace audd

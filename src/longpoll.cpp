// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#include <audd/longpoll.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include <audd/callback.hpp>
#include <audd/client.hpp>
#include <audd/error.hpp>

#include "internal/client_internal.hpp"
#include "internal/http_client.hpp"
#include "internal/json_parse.hpp"
#include "internal/md5.hpp"

namespace audd {

namespace {
constexpr const char* kApiBase = "https://api.audd.io";
} // anonymous

std::string derive_longpoll_category(const std::string& api_token, int radio_id) {
    std::string inner = audd::internal::md5_hex(api_token);
    std::string outer = audd::internal::md5_hex(inner + std::to_string(radio_id));
    return outer.substr(0, 9);
}

// Demuxed queue + condition variable: producer pushes typed events, consumer
// pulls. Once `closed` is set, producer no longer pushes and consumers
// receive nullopt.
template <typename T>
class DemuxQueue {
public:
    void push(T v) {
        {
            std::lock_guard<std::mutex> lk(m_);
            if (closed_) return;
            q_.push(std::move(v));
        }
        cv_.notify_one();
    }
    void close() {
        {
            std::lock_guard<std::mutex> lk(m_);
            closed_ = true;
        }
        cv_.notify_all();
    }
    std::optional<T> pull() {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [&] { return !q_.empty() || closed_; });
        if (!q_.empty()) {
            T v = std::move(q_.front());
            q_.pop();
            return v;
        }
        return std::nullopt;
    }
private:
    std::mutex                  m_;
    std::condition_variable     cv_;
    std::queue<T>               q_;
    bool                        closed_ = false;
};

struct LongpollPoll::Impl {
    DemuxQueue<StreamCallbackMatch>          matches;
    DemuxQueue<StreamCallbackNotification>   notifications;
    DemuxQueue<std::exception_ptr>           errors;

    std::atomic<bool> closed{false};
    std::thread       worker;

    void close_all() {
        closed.store(true);
        matches.close();
        notifications.close();
        errors.close();
    }

    ~Impl() {
        close_all();
        if (worker.joinable()) worker.join();
    }
};

LongpollPoll::LongpollPoll() = default;
LongpollPoll::LongpollPoll(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
LongpollPoll::~LongpollPoll() {
    if (impl_) impl_->close_all();
}
LongpollPoll::LongpollPoll(LongpollPoll&&) noexcept = default;
LongpollPoll& LongpollPoll::operator=(LongpollPoll&&) noexcept = default;

std::optional<StreamCallbackMatch> LongpollPoll::next_match() {
    if (!impl_) return std::nullopt;
    return impl_->matches.pull();
}

std::optional<StreamCallbackNotification> LongpollPoll::next_notification() {
    if (!impl_) return std::nullopt;
    return impl_->notifications.pull();
}

std::optional<std::exception_ptr> LongpollPoll::next_error() {
    if (!impl_) return std::nullopt;
    return impl_->errors.pull();
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
    // Pump matches and notifications in two threads, errors on the calling
    // thread (it's terminal).
    std::thread tm([&] {
        while (auto m = impl_->matches.pull()) {
            if (on_match) on_match(std::move(*m));
        }
    });
    std::thread tn([&] {
        while (auto n = impl_->notifications.pull()) {
            if (on_notification) on_notification(std::move(*n));
        }
    });
    while (auto e = impl_->errors.pull()) {
        if (on_error) on_error(*e);
        impl_->close_all();
        break;
    }
    if (tm.joinable()) tm.join();
    if (tn.joinable()) tn.join();
}

void LongpollPoll::close() noexcept {
    if (impl_) impl_->close_all();
}

namespace {

// Reads a `timestamp` field (if present and integral) to advance the poll
// cursor. Never throws.
void advance_since(const nlohmann::json& body, long long& cur_since) {
    if (!body.is_object()) return;
    auto t_it = body.find("timestamp");
    if (t_it != body.end() && !t_it->is_null()) {
        try { cur_since = t_it->get<long long>(); } catch (...) {}
    }
}

} // anonymous

// Implementation of the StreamsClient::longpoll factory hook.
LongpollPoll start_longpoll_(AudD* parent, std::string category, LongpollOptions opts) {
    auto impl = std::make_unique<LongpollPoll::Impl>();
    auto* impl_ptr = impl.get();
    if (opts.timeout_seconds <= 0) opts.timeout_seconds = 50;
    long long since_time = opts.since_time;
    int timeout = opts.timeout_seconds;

    impl_ptr->worker = std::thread([parent, category = std::move(category),
                                    since_time, timeout, impl_ptr]() mutable {
        std::string url = std::string(kApiBase) + "/longpoll/";
        long long cur_since = since_time;

        // Bounded exponential-ish backoff for transient connection errors.
        // A long-lived poll expects occasional connection blips; reconnect
        // rather than terminating the subscription.
        constexpr std::chrono::milliseconds kBackoffBase{500};
        constexpr std::chrono::milliseconds kBackoffCap{30000};
        std::chrono::milliseconds backoff = kBackoffBase;

        // Sleeps for `d`, but wakes early if the poll is closed. Returns
        // false if the poll was closed while sleeping.
        auto sleep_or_stop = [impl_ptr](std::chrono::milliseconds d) -> bool {
            const auto deadline = std::chrono::steady_clock::now() + d;
            while (std::chrono::steady_clock::now() < deadline) {
                if (impl_ptr->closed.load()) return false;
                auto remaining = deadline - std::chrono::steady_clock::now();
                std::this_thread::sleep_for(
                    std::min<std::chrono::milliseconds>(
                        std::chrono::milliseconds(100),
                        std::chrono::duration_cast<std::chrono::milliseconds>(remaining)));
            }
            return !impl_ptr->closed.load();
        };

        try {
            while (!impl_ptr->closed.load()) {
                std::map<std::string, std::string> params;
                params["category"] = category;
                params["timeout"]  = std::to_string(timeout);
                if (cur_since > 0) params["since_time"] = std::to_string(cur_since);

                internal::HttpResponse resp;
                try {
                    resp = parent->internal()->standard_http->get(url, params);
                } catch (const AudDConnectionError&) {
                    // Transient blip: back off and reconnect. Only a
                    // user-initiated stop (checked in sleep_or_stop) ends
                    // the loop here.
                    if (!sleep_or_stop(backoff)) return;
                    backoff = std::min(backoff * 2, kBackoffCap);
                    continue;
                }
                if (impl_ptr->closed.load()) return;

                // A successful round-trip resets the backoff.
                backoff = kBackoffBase;

                auto disp = internal::classify_longpoll_response(
                    resp.http_status, resp.json_body, resp.raw_body.empty());

                if (disp == internal::LongpollDisposition::Terminal) {
                    AudDApiError e(0, "Longpoll endpoint returned HTTP " +
                                       std::to_string(resp.http_status),
                                   resp.http_status, resp.request_id);
                    e.raw_response = resp.raw_body;
                    impl_ptr->errors.push(std::make_exception_ptr(e));
                    impl_ptr->close_all();
                    return;
                }
                if (disp == internal::LongpollDisposition::Skip) {
                    // An empty body is not a terminal API error; keep polling.
                    continue;
                }
                if (disp == internal::LongpollDisposition::KeepAlive) {
                    advance_since(resp.json_body, cur_since);
                    continue;
                }
                // disp == Event: try to parse the body. An unparseable or
                // unknown event body is non-terminal — skip it and keep the
                // subscription alive.
                try {
                    auto ev = parse_callback(resp.raw_body);
                    if (auto* m = std::get_if<StreamCallbackMatch>(&ev)) {
                        impl_ptr->matches.push(std::move(*m));
                    } else if (auto* n = std::get_if<StreamCallbackNotification>(&ev)) {
                        impl_ptr->notifications.push(std::move(*n));
                    }
                } catch (const std::exception&) {
                    continue;
                }
                advance_since(resp.json_body, cur_since);
            }
        } catch (...) {
            impl_ptr->errors.push(std::current_exception());
            impl_ptr->close_all();
        }
    });

    return LongpollPoll(std::move(impl));
}

} // namespace audd

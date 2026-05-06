// SPDX-License-Identifier: MIT
// Copyright (c) AudD <https://audd.io>

#include <audd/longpoll.hpp>

#include <atomic>
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

bool is_keepalive(const nlohmann::json& body) {
    if (!body.is_object()) return false;
    if (body.contains("result")) return false;
    if (body.contains("notification")) return false;
    return body.contains("timeout");
}

} // anonymous

// Implementation of the StreamsClient::longpoll factory hook.
LongpollPoll start_longpoll_(AudD* parent, std::string category, LongpollOptions opts) {
    auto impl = std::make_unique<LongpollPoll::Impl>();
    auto* impl_ptr = impl.get();
    if (opts.timeout_seconds <= 0) opts.timeout_seconds = 50;
    int since_time = opts.since_time;
    int timeout = opts.timeout_seconds;

    impl_ptr->worker = std::thread([parent, category = std::move(category),
                                    since_time, timeout, impl_ptr]() mutable {
        std::string url = std::string(kApiBase) + "/longpoll/";
        int cur_since = since_time;
        try {
            while (!impl_ptr->closed.load()) {
                std::map<std::string, std::string> params;
                params["category"] = category;
                params["timeout"]  = std::to_string(timeout);
                if (cur_since > 0) params["since_time"] = std::to_string(cur_since);

                internal::HttpResponse resp;
                try {
                    resp = parent->internal()->standard_http->get(url, params);
                } catch (const AuddConnectionError&) {
                    impl_ptr->errors.push(std::current_exception());
                    impl_ptr->close_all();
                    return;
                }
                if (impl_ptr->closed.load()) return;

                if (resp.http_status >= 400) {
                    AuddApiError e(0, "Longpoll endpoint returned HTTP " +
                                       std::to_string(resp.http_status),
                                   resp.http_status, resp.request_id);
                    e.raw_response = resp.raw_body;
                    impl_ptr->errors.push(std::make_exception_ptr(e));
                    impl_ptr->close_all();
                    return;
                }
                if (resp.raw_body.empty()) {
                    AuddSerializationError e("Longpoll response was empty");
                    impl_ptr->errors.push(std::make_exception_ptr(e));
                    impl_ptr->close_all();
                    return;
                }
                if (is_keepalive(resp.json_body)) {
                    auto t_it = resp.json_body.find("timestamp");
                    if (t_it != resp.json_body.end() && !t_it->is_null()) {
                        try { cur_since = t_it->get<int>(); } catch (...) {}
                    }
                    continue;
                }
                try {
                    auto ev = parse_callback(resp.raw_body);
                    if (auto* m = std::get_if<StreamCallbackMatch>(&ev)) {
                        impl_ptr->matches.push(std::move(*m));
                    } else if (auto* n = std::get_if<StreamCallbackNotification>(&ev)) {
                        impl_ptr->notifications.push(std::move(*n));
                    }
                } catch (const std::exception&) {
                    impl_ptr->errors.push(std::current_exception());
                    impl_ptr->close_all();
                    return;
                }
                if (resp.json_body.is_object()) {
                    auto t_it = resp.json_body.find("timestamp");
                    if (t_it != resp.json_body.end() && !t_it->is_null()) {
                        try { cur_since = t_it->get<int>(); } catch (...) {}
                    }
                }
            }
        } catch (...) {
            impl_ptr->errors.push(std::current_exception());
            impl_ptr->close_all();
        }
    });

    return LongpollPoll(std::move(impl));
}

} // namespace audd

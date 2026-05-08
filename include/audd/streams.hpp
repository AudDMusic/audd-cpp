// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#ifndef AUDD_STREAMS_HPP
#define AUDD_STREAMS_HPP

#include <future>
#include <optional>
#include <string>
#include <vector>

#include <audd/longpoll.hpp>
#include <audd/recognition.hpp>

namespace audd {

class AudD; // forward

// SetCallbackUrlOptions controls AudD::Streams::set_callback_url.
struct SetCallbackUrlOptions {
    // return_metadata, if non-empty, is added as a `?return=<csv>` query
    // parameter on the callback URL. If the URL already has a `return`
    // param, set_callback_url throws rather than silently overwriting.
    std::vector<std::string> return_metadata;
};

// AddStreamRequest describes a stream to subscribe to.
struct AddStreamRequest {
    // url is the stream URL. Accepts direct stream URLs (DASH, Icecast,
    // HLS, m3u/m3u8) and shortcuts: twitch:<channel>, youtube:<video_id>,
    // youtube-ch:<channel_id>.
    std::string url;
    // radio_id is the integer ID you assign to this stream slot.
    int radio_id = 0;
    // callbacks: pass "before" to fire callbacks at song start (default is
    // at song end).
    std::string callbacks;
};

// StreamsClient handles stream-management methods. Reach via AudD::streams().
class StreamsClient {
public:
    explicit StreamsClient(AudD* parent) noexcept : parent_(parent) {}

    // set_callback_url tells AudD to POST recognition results for your
    // account to `url`. opts may add `?return=<csv>` to the callback URL.
    void set_callback_url(const std::string& url,
                          const SetCallbackUrlOptions& opts = {});
    std::future<void> set_callback_url_async(std::string url,
                                             SetCallbackUrlOptions opts = {});

    // get_callback_url returns the configured callback URL, or "" if none.
    std::string get_callback_url();
    std::future<std::string> get_callback_url_async();

    // add subscribes the account to the given stream.
    void add(const AddStreamRequest& req);
    std::future<void> add_async(AddStreamRequest req);

    // set_url changes the stream URL for an existing radio_id.
    void set_url(int radio_id, const std::string& url);
    std::future<void> set_url_async(int radio_id, std::string url);

    // del removes a stream subscription.
    void del(int radio_id);
    std::future<void> del_async(int radio_id);

    // list returns all configured streams.
    std::vector<Stream> list();
    std::future<std::vector<Stream>> list_async();

    // derive_longpoll_category computes the 9-char longpoll category locally
    // for the parent client's token + radio_id. No network call.
    std::string derive_longpoll_category(int radio_id) const;

    // longpoll starts a long-poll subscription. Returns a LongpollPoll
    // whose three streams (matches / notifications / errors) are filled by
    // a background thread.
    //
    // On entry, runs a preflight get_callback_url() unless
    // opts.skip_callback_check is set — catches the common silent-failure
    // mode where no callback URL is configured.
    LongpollPoll longpoll(const std::string& category,
                          const LongpollOptions& opts = {});

    // longpoll(int radio_id, ...) is the one-step entry point for the common
    // case where the caller has both the api_token (held by this client) and
    // a radio_id. Derives the 9-char category locally, then delegates to the
    // category-string overload above. The string-form overload remains for
    // tokenless consumers (e.g. a server hands a pre-derived category to a
    // browser/mobile client without sharing the api_token).
    LongpollPoll longpoll(int radio_id,
                          const LongpollOptions& opts = {});

private:
    AudD* parent_;
};

} // namespace audd

#endif // AUDD_STREAMS_HPP

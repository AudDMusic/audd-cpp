// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#ifndef AUDD_CLIENT_HPP
#define AUDD_CLIENT_HPP

#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <audd/advanced.hpp>
#include <audd/custom_catalog.hpp>
#include <audd/recognition.hpp>
#include <audd/source.hpp>
#include <audd/streams.hpp>

namespace audd {

// AudDEvent is emitted by the SDK request lifecycle when an on_event hook is
// registered. Frozen plain-data; never carries the api_token or request body
// bytes.
struct AudDEvent {
    enum class Kind { Request, Response, Exception };
    Kind                      kind        = Kind::Request;
    std::string               method;       // AudD method name, e.g. "recognize", "addStream"
    std::string               url;
    std::string               request_id;   // X-Request-Id header value, if present
    int                       http_status = 0; // 0 for request/exception kinds
    std::chrono::milliseconds elapsed{0};
    int                       error_code  = 0;
};

// ClientConfig bundles the optional knobs accepted by AudD's constructor.
// Defaults match audd-go: 3 attempts, 500ms backoff, 90s standard /
// 1h enterprise timeouts, deprecation-warning logged via std::cerr.
struct ClientConfig {
    std::chrono::milliseconds standard_timeout   {std::chrono::seconds(90)};
    std::chrono::milliseconds enterprise_timeout {std::chrono::hours(1)};
    int                       max_attempts   = 3;
    std::chrono::milliseconds backoff_factor {std::chrono::milliseconds(500)};

    // on_deprecation is called when the server returns a code-51 deprecation
    // warning alongside a usable result. Default writes to std::cerr.
    std::function<void(const std::string&)> on_deprecation;
    // on_event is called for every API-call lifecycle event (request /
    // response / exception). Off by default.
    std::function<void(const AudDEvent&)>   on_event;
};

// AudD is the top-level SDK client.
//
// Construct with a token string (or empty to fall through to the
// AUDD_API_TOKEN env var). Sub-clients (streams(), custom_catalog(),
// advanced()) share state with the parent; the parent owns them.
//
// Thread-safe: the AudD client and its sub-clients are safe for concurrent
// use across threads.
class AudD {
public:
    // Construct with token only.
    explicit AudD(const std::string& token = "");
    // Construct with token + config.
    AudD(const std::string& token, ClientConfig config);

    ~AudD();
    AudD(const AudD&) = delete;
    AudD& operator=(const AudD&) = delete;
    AudD(AudD&&) = delete;
    AudD& operator=(AudD&&) = delete;

    // strict construction: throws AudDMissingApiTokenError if no token is
    // supplied and AUDD_API_TOKEN is unset. Returns a unique_ptr so the
    // result is movable (AudD itself is non-movable due to internal mutex
    // state).
    static std::unique_ptr<AudD> strict(const std::string& token = "");
    static std::unique_ptr<AudD> strict(const std::string& token, ClientConfig config);

    // api_token returns the in-effect token (after any rotation).
    std::string api_token() const;

    // set_api_token atomically swaps the token used for subsequent
    // requests. In-flight requests continue with the old token. Throws on
    // empty new token.
    void set_api_token(const std::string& new_token);

    // recognize sends `source` to the standard recognize endpoint and
    // returns the typed result. Returns std::nullopt when the server
    // returns status=success with result=null (no match).
    //
    // `source` accepts:
    //   - std::string — an HTTP(S) URL or an existing file path
    //   - SourceUrl / SourceFilePath / SourceBytes — explicit forms
    std::optional<RecognitionResult>
    recognize(const Source& source, const RecognizeOptions& opts = {});

    std::future<std::optional<RecognitionResult>>
    recognize_async(Source source, RecognizeOptions opts = {});

    // recognize_enterprise sends `source` to the enterprise endpoint and
    // returns the matches across all chunks. Empty vector when no matches.
    std::vector<EnterpriseMatch>
    recognize_enterprise(const Source& source, const EnterpriseOptions& opts = {});

    std::future<std::vector<EnterpriseMatch>>
    recognize_enterprise_async(Source source, EnterpriseOptions opts = {});

    // Sub-clients. Lazily constructed; all share state with the parent.
    StreamsClient&       streams();
    CustomCatalogClient& custom_catalog();
    AdvancedClient&      advanced();

    // Internal accessors used by sub-clients. Not part of the public API.
    struct Internal;
    Internal* internal() noexcept { return internal_.get(); }

private:
    std::unique_ptr<Internal>                internal_;
    std::unique_ptr<StreamsClient>           streams_;
    std::unique_ptr<CustomCatalogClient>     custom_catalog_;
    std::unique_ptr<AdvancedClient>          advanced_;
    mutable std::mutex                       sub_clients_mutex_;
};

} // namespace audd

#endif // AUDD_CLIENT_HPP

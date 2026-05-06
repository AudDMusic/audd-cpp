// SPDX-License-Identifier: MIT
// Copyright (c) AudD <https://audd.io>

#ifndef AUDD_ERROR_HPP
#define AUDD_ERROR_HPP

#include <exception>
#include <map>
#include <stdexcept>
#include <string>

namespace audd {

// ErrorCategory enumerates the broad categories of errors raised by the SDK.
// Use AuddApiError::category() to discriminate without staring at numeric
// codes. The category is computed from the AudD error_code via
// sentinel_for_code().
enum class ErrorCategory {
    Authentication,      // 900 / 901 / 903 — token problems
    Quota,               // 902 — quota exceeded
    Subscription,        // 904 / 905 — endpoint not enabled on token
    CustomCatalogAccess, // overlay over Subscription for custom-catalog calls
    InvalidRequest,      // 50 / 51 / 600 / 601 / 602 / 700 / 701 / 702 / 906
    InvalidAudio,        // 300 / 400 / 500
    RateLimit,           // 611
    StreamLimit,         // 610
    NotReleased,         // 907
    Blocked,             // 19 / 31337
    NeedsUpdate,         // 20
    Server,              // 5xx, non-JSON gateway responses, fallthrough
    Connection,          // transport-level: DNS, TCP, TLS, timeout
    Serialization,       // 2xx with unparseable body
};

// category_name returns a stable string label for a category — useful for
// log fields.
inline const char* category_name(ErrorCategory c) noexcept {
    switch (c) {
        case ErrorCategory::Authentication:      return "authentication";
        case ErrorCategory::Quota:               return "quota";
        case ErrorCategory::Subscription:        return "subscription";
        case ErrorCategory::CustomCatalogAccess: return "custom_catalog_access";
        case ErrorCategory::InvalidRequest:      return "invalid_request";
        case ErrorCategory::InvalidAudio:        return "invalid_audio";
        case ErrorCategory::RateLimit:           return "rate_limit";
        case ErrorCategory::StreamLimit:         return "stream_limit";
        case ErrorCategory::NotReleased:         return "not_released";
        case ErrorCategory::Blocked:             return "blocked";
        case ErrorCategory::NeedsUpdate:         return "needs_update";
        case ErrorCategory::Server:              return "server";
        case ErrorCategory::Connection:          return "connection";
        case ErrorCategory::Serialization:       return "serialization";
    }
    return "unknown";
}

// sentinel_for_code maps an AudD numeric code to its category. Used by both
// the typed AuddApiError and the test suite. Mirrors audd-go's mapping.
inline ErrorCategory sentinel_for_code(int code) noexcept {
    switch (code) {
        case 900: case 901: case 903:
            return ErrorCategory::Authentication;
        case 902:
            return ErrorCategory::Quota;
        case 904: case 905:
            return ErrorCategory::Subscription;
        case 50: case 51: case 600: case 601: case 602:
        case 700: case 701: case 702: case 906:
            return ErrorCategory::InvalidRequest;
        case 300: case 400: case 500:
            return ErrorCategory::InvalidAudio;
        case 610: return ErrorCategory::StreamLimit;
        case 611: return ErrorCategory::RateLimit;
        case 907: return ErrorCategory::NotReleased;
        case 19:  case 31337:
            return ErrorCategory::Blocked;
        case 20:
            return ErrorCategory::NeedsUpdate;
        default:
            return ErrorCategory::Server;
    }
}

// AuddError is the abstract base for every error raised from this SDK.
// Catch-all sites can write `catch (const audd::AuddError& e) { ... }`;
// finer-grained discrimination is via category() or dynamic_cast.
class AuddError : public std::runtime_error {
public:
    explicit AuddError(std::string what)
        : std::runtime_error(std::move(what)) {}
    virtual ErrorCategory category() const noexcept = 0;
};

// AuddApiError is raised for any `status: error` response from the AudD API,
// and for HTTP non-2xx responses with a non-JSON body (mapped to error_code=0
// + category=Server).
class AuddApiError : public AuddError {
public:
    int                                error_code   = 0;   // AudD numeric code (900, 904, ...). 0 = HTTP-only failure with no JSON body.
    std::string                        message;            // Server-supplied human-readable message.
    int                                http_status  = 0;   // HTTP response status code.
    std::string                        request_id;         // X-Request-Id header from the response, if present.
    std::string                        request_method;     // Server's `request_api_method` field (informational).
    std::string                        branded_message;    // "Artist — Title" string from a branded-denial response, if any. Empty otherwise.
    std::map<std::string, std::string> requested_params;   // Server's redacted echo of the request fields.
    std::string                        raw_response;       // Unparsed body. Empty if no body.

    AuddApiError(int code, std::string msg, int http_status_, std::string request_id_)
        : AuddError(format_what(code, msg))
        , error_code(code)
        , message(std::move(msg))
        , http_status(http_status_)
        , request_id(std::move(request_id_)) {}

    ErrorCategory category() const noexcept override {
        return sentinel_for_code(error_code);
    }

private:
    static std::string format_what(int code, const std::string& m) {
        return "[#" + std::to_string(code) + "] " + m;
    }
};

// AuddCustomCatalogAccessError is raised specifically from CustomCatalog::add
// when the token lacks subscription access (codes 904/905 in that context).
// Carries a long, friendlier message that disambiguates the common
// custom-catalog footgun (custom catalog isn't recognition).
class AuddCustomCatalogAccessError : public AuddApiError {
public:
    AuddCustomCatalogAccessError(int code, std::string server_message,
                                 int http_status_, std::string request_id_)
        : AuddApiError(code, build_message(server_message),
                       http_status_, std::move(request_id_))
        , server_message(std::move(server_message)) {}

    std::string server_message;

    ErrorCategory category() const noexcept override {
        return ErrorCategory::CustomCatalogAccess;
    }

private:
    static std::string build_message(const std::string& server_msg) {
        return std::string(
            "Adding songs to your custom catalog requires enterprise access "
            "that isn't enabled on your account.\n\n"
            "Note: the custom-catalog endpoint is for adding songs to your "
            "private fingerprint database, not for music recognition. If you "
            "intended to identify music, use client.recognize(...) (or "
            "client.recognize_enterprise(...) for files longer than 25 "
            "seconds) instead.\n\n"
            "To request custom-catalog access, contact api@audd.io.\n\n"
            "[Server message: ") + server_msg + "]";
    }
};

// AuddConnectionError wraps a transport-level failure (DNS, TCP, TLS,
// timeout). Use category() == ErrorCategory::Connection to match.
class AuddConnectionError : public AuddError {
public:
    explicit AuddConnectionError(std::string msg)
        : AuddError("audd: connection error: " + msg) {}

    ErrorCategory category() const noexcept override {
        return ErrorCategory::Connection;
    }
};

// AuddSerializationError is returned for 2xx HTTP responses whose body is
// not parseable as the expected JSON shape, or for callback bodies that fail
// to parse.
class AuddSerializationError : public AuddError {
public:
    std::string raw_text;

    explicit AuddSerializationError(std::string msg, std::string raw = "")
        : AuddError("audd: serialization error: " + msg)
        , raw_text(std::move(raw)) {}

    ErrorCategory category() const noexcept override {
        return ErrorCategory::Serialization;
    }
};

// ErrMissingApiToken is thrown by AudD::strict() when neither a token argument
// nor the AUDD_API_TOKEN env var is set.
class AuddMissingApiTokenError : public AuddError {
public:
    AuddMissingApiTokenError()
        : AuddError("audd: api_token not supplied and AUDD_API_TOKEN env var "
                    "is unset; get a token at https://dashboard.audd.io") {}
    ErrorCategory category() const noexcept override {
        return ErrorCategory::Authentication;
    }
};

} // namespace audd

#endif // AUDD_ERROR_HPP

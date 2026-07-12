// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)
//
// Internal retry helper. Exposed in this header so tests can verify the
// per-call invariant (e.g. max_attempts=1 means exactly one invocation,
// regardless of whether the call returns or throws AudDConnectionError).

#ifndef AUDD_INTERNAL_RETRY_HPP
#define AUDD_INTERNAL_RETRY_HPP

#include <chrono>
#include <thread>

#include <audd/error.hpp>

namespace audd::internal {

// RetryGate selects which AudDConnectionErrors are eligible for retry.
enum class RetryGate {
    // Idempotent reads (streams list, callback URL reads, longpoll fetches):
    // any transport failure is safe to retry.
    AllConnectionErrors,
    // Recognition and mutating POSTs: retry only failures that provably
    // happened before the request body was sent
    // (AudDConnectionError::failed_before_upload()). Once the body may have
    // reached the server, the metered work may already be done — and billed —
    // so the request is never silently re-sent.
    PreUploadOnly,
};

// retry_on_connection_error invokes `f()` up to max_attempts times, retrying
// only on AudDConnectionError that passes `gate` (see RetryGate).
// Linear-doubling backoff between attempts.
//
// max_attempts == 1 disables retry entirely: the first AudDConnectionError
// is rethrown immediately. Used by metered endpoints (custom_catalog().add)
// where a silent re-upload could double-charge for the same fingerprinting.
//
// Non-AudDConnectionError exceptions (including AudDApiError on a 5xx
// response) propagate on the first attempt. HTTP-status retry is not
// performed at this layer — the HttpClient returns a populated
// HttpResponse for any HTTP status, and per-request handling decides what
// to do with it.
template <typename F>
auto retry_on_connection_error(int max_attempts,
                               std::chrono::milliseconds backoff,
                               F&& f,
                               RetryGate gate = RetryGate::AllConnectionErrors)
    -> decltype(f()) {
    if (max_attempts < 1) max_attempts = 1;
    int attempt = 0;
    std::chrono::milliseconds wait = backoff;
    for (;;) {
        try {
            return f();
        } catch (const AudDConnectionError& e) {
            attempt++;
            const bool retryable = gate == RetryGate::AllConnectionErrors ||
                                   e.failed_before_upload();
            if (!retryable || attempt >= max_attempts) throw;
            std::this_thread::sleep_for(wait);
            wait *= 2;
        }
    }
}

} // namespace audd::internal

#endif // AUDD_INTERNAL_RETRY_HPP

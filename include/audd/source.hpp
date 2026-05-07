// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)

#ifndef AUDD_SOURCE_HPP
#define AUDD_SOURCE_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace audd {

// SourceUrl is a typed wrapper around an HTTP(S) URL. Construct explicitly
// when you want to disambiguate from file paths (rare — the std::string
// auto-detect handles HTTP(S) prefixes).
struct SourceUrl { std::string url; };

// SourceFilePath is a typed wrapper around a local-file path.
struct SourceFilePath { std::string path; };

// SourceBytes wraps raw audio bytes (e.g. from a memory buffer).
// `mime_type` defaults to "application/octet-stream" if left empty.
struct SourceBytes {
    std::vector<std::uint8_t> bytes;
    std::string               name      = "upload.bin";
    std::string               mime_type = "application/octet-stream";
};

// Source is the typed sum of accepted recognition inputs:
//   - std::string is auto-classified: HTTP(S) URL when it starts with
//     "http://" / "https://"; otherwise an existing file path.
//   - SourceUrl / SourceFilePath force the classification.
//   - SourceBytes is raw bytes.
using Source = std::variant<std::string, SourceUrl, SourceFilePath, SourceBytes>;

} // namespace audd

#endif // AUDD_SOURCE_HPP

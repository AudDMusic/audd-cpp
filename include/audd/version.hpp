// SPDX-License-Identifier: MIT
// Copyright (c) AudD <https://audd.io>
//
// Official C++ SDK for the AudD music recognition API.
// See https://docs.audd.io for the API reference and
// https://github.com/AudDMusic/audd-cpp for source.

#ifndef AUDD_VERSION_HPP
#define AUDD_VERSION_HPP

#define AUDD_VERSION "1.5.2"

namespace audd {

// version() returns the SDK version string. Reported in the User-Agent header.
inline const char* version() noexcept { return AUDD_VERSION; }

} // namespace audd

#endif // AUDD_VERSION_HPP

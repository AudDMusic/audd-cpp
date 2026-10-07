// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)
//
// Official C++ SDK for the AudD music recognition API.
// See https://docs.audd.io for the API reference and
// https://github.com/AudDMusic/audd-cpp for source.

#ifndef AUDD_VERSION_HPP
#define AUDD_VERSION_HPP

#define AUDD_CPP_VERSION "1.5.16"

// Compatibility alias. Defined only if a co-resident SDK (e.g. audd-c) hasn't
// already claimed AUDD_VERSION, so both can share a project without collision.
#ifndef AUDD_VERSION
#define AUDD_VERSION AUDD_CPP_VERSION
#endif

namespace audd {

// version() returns the SDK version string. Reported in the User-Agent header.
inline const char* version() noexcept { return AUDD_CPP_VERSION; }

} // namespace audd

#endif // AUDD_VERSION_HPP

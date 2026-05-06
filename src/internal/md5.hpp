// SPDX-License-Identifier: MIT
// Copyright (c) AudD <https://audd.io>
//
// Internal MD5 wrapper. The longpoll-category derivation uses MD5 by spec
// (docs.audd.io/streams.md). It's not used as a security primitive.

#ifndef AUDD_INTERNAL_MD5_HPP
#define AUDD_INTERNAL_MD5_HPP

#include <string>

namespace audd::internal {

// md5_hex returns the lowercase hex MD5 digest of `s`.
std::string md5_hex(const std::string& s);

} // namespace audd::internal

#endif // AUDD_INTERNAL_MD5_HPP

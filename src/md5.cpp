// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)
//
// Public-domain reference implementation of MD5, adapted from RFC 1321.
// MD5 is required for the longpoll-category derivation (docs.audd.io
// formula). Not used as a cryptographic primitive.

#include "internal/md5.hpp"

#include <cstdint>
#include <cstring>
#include <string>

namespace audd::internal {

namespace {

struct Md5Ctx {
    std::uint32_t state[4];
    std::uint64_t bitcount;
    std::uint8_t  buffer[64];
};

constexpr std::uint32_t S11 = 7, S12 = 12, S13 = 17, S14 = 22;
constexpr std::uint32_t S21 = 5, S22 =  9, S23 = 14, S24 = 20;
constexpr std::uint32_t S31 = 4, S32 = 11, S33 = 16, S34 = 23;
constexpr std::uint32_t S41 = 6, S42 = 10, S43 = 15, S44 = 21;

inline std::uint32_t F(std::uint32_t x, std::uint32_t y, std::uint32_t z) { return (x & y) | (~x & z); }
inline std::uint32_t G(std::uint32_t x, std::uint32_t y, std::uint32_t z) { return (x & z) | (y & ~z); }
inline std::uint32_t H(std::uint32_t x, std::uint32_t y, std::uint32_t z) { return x ^ y ^ z; }
inline std::uint32_t I(std::uint32_t x, std::uint32_t y, std::uint32_t z) { return y ^ (x | ~z); }
inline std::uint32_t rotl(std::uint32_t x, std::uint32_t n) { return (x << n) | (x >> (32 - n)); }

inline void FF(std::uint32_t& a, std::uint32_t b, std::uint32_t c, std::uint32_t d, std::uint32_t x, std::uint32_t s, std::uint32_t ac) {
    a = b + rotl(a + F(b, c, d) + x + ac, s);
}
inline void GG(std::uint32_t& a, std::uint32_t b, std::uint32_t c, std::uint32_t d, std::uint32_t x, std::uint32_t s, std::uint32_t ac) {
    a = b + rotl(a + G(b, c, d) + x + ac, s);
}
inline void HH(std::uint32_t& a, std::uint32_t b, std::uint32_t c, std::uint32_t d, std::uint32_t x, std::uint32_t s, std::uint32_t ac) {
    a = b + rotl(a + H(b, c, d) + x + ac, s);
}
inline void II(std::uint32_t& a, std::uint32_t b, std::uint32_t c, std::uint32_t d, std::uint32_t x, std::uint32_t s, std::uint32_t ac) {
    a = b + rotl(a + I(b, c, d) + x + ac, s);
}

void md5_init(Md5Ctx& ctx) {
    ctx.bitcount = 0;
    ctx.state[0] = 0x67452301;
    ctx.state[1] = 0xefcdab89;
    ctx.state[2] = 0x98badcfe;
    ctx.state[3] = 0x10325476;
}

void md5_transform(std::uint32_t state[4], const std::uint8_t block[64]) {
    std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    std::uint32_t x[16];
    for (int i = 0; i < 16; ++i) {
        x[i] = (std::uint32_t)block[i * 4] |
               ((std::uint32_t)block[i * 4 + 1] << 8) |
               ((std::uint32_t)block[i * 4 + 2] << 16) |
               ((std::uint32_t)block[i * 4 + 3] << 24);
    }
    // Round 1
    FF(a,b,c,d, x[0],  S11, 0xd76aa478); FF(d,a,b,c, x[1],  S12, 0xe8c7b756);
    FF(c,d,a,b, x[2],  S13, 0x242070db); FF(b,c,d,a, x[3],  S14, 0xc1bdceee);
    FF(a,b,c,d, x[4],  S11, 0xf57c0faf); FF(d,a,b,c, x[5],  S12, 0x4787c62a);
    FF(c,d,a,b, x[6],  S13, 0xa8304613); FF(b,c,d,a, x[7],  S14, 0xfd469501);
    FF(a,b,c,d, x[8],  S11, 0x698098d8); FF(d,a,b,c, x[9],  S12, 0x8b44f7af);
    FF(c,d,a,b, x[10], S13, 0xffff5bb1); FF(b,c,d,a, x[11], S14, 0x895cd7be);
    FF(a,b,c,d, x[12], S11, 0x6b901122); FF(d,a,b,c, x[13], S12, 0xfd987193);
    FF(c,d,a,b, x[14], S13, 0xa679438e); FF(b,c,d,a, x[15], S14, 0x49b40821);
    // Round 2
    GG(a,b,c,d, x[1],  S21, 0xf61e2562); GG(d,a,b,c, x[6],  S22, 0xc040b340);
    GG(c,d,a,b, x[11], S23, 0x265e5a51); GG(b,c,d,a, x[0],  S24, 0xe9b6c7aa);
    GG(a,b,c,d, x[5],  S21, 0xd62f105d); GG(d,a,b,c, x[10], S22, 0x02441453);
    GG(c,d,a,b, x[15], S23, 0xd8a1e681); GG(b,c,d,a, x[4],  S24, 0xe7d3fbc8);
    GG(a,b,c,d, x[9],  S21, 0x21e1cde6); GG(d,a,b,c, x[14], S22, 0xc33707d6);
    GG(c,d,a,b, x[3],  S23, 0xf4d50d87); GG(b,c,d,a, x[8],  S24, 0x455a14ed);
    GG(a,b,c,d, x[13], S21, 0xa9e3e905); GG(d,a,b,c, x[2],  S22, 0xfcefa3f8);
    GG(c,d,a,b, x[7],  S23, 0x676f02d9); GG(b,c,d,a, x[12], S24, 0x8d2a4c8a);
    // Round 3
    HH(a,b,c,d, x[5],  S31, 0xfffa3942); HH(d,a,b,c, x[8],  S32, 0x8771f681);
    HH(c,d,a,b, x[11], S33, 0x6d9d6122); HH(b,c,d,a, x[14], S34, 0xfde5380c);
    HH(a,b,c,d, x[1],  S31, 0xa4beea44); HH(d,a,b,c, x[4],  S32, 0x4bdecfa9);
    HH(c,d,a,b, x[7],  S33, 0xf6bb4b60); HH(b,c,d,a, x[10], S34, 0xbebfbc70);
    HH(a,b,c,d, x[13], S31, 0x289b7ec6); HH(d,a,b,c, x[0],  S32, 0xeaa127fa);
    HH(c,d,a,b, x[3],  S33, 0xd4ef3085); HH(b,c,d,a, x[6],  S34, 0x04881d05);
    HH(a,b,c,d, x[9],  S31, 0xd9d4d039); HH(d,a,b,c, x[12], S32, 0xe6db99e5);
    HH(c,d,a,b, x[15], S33, 0x1fa27cf8); HH(b,c,d,a, x[2],  S34, 0xc4ac5665);
    // Round 4
    II(a,b,c,d, x[0],  S41, 0xf4292244); II(d,a,b,c, x[7],  S42, 0x432aff97);
    II(c,d,a,b, x[14], S43, 0xab9423a7); II(b,c,d,a, x[5],  S44, 0xfc93a039);
    II(a,b,c,d, x[12], S41, 0x655b59c3); II(d,a,b,c, x[3],  S42, 0x8f0ccc92);
    II(c,d,a,b, x[10], S43, 0xffeff47d); II(b,c,d,a, x[1],  S44, 0x85845dd1);
    II(a,b,c,d, x[8],  S41, 0x6fa87e4f); II(d,a,b,c, x[15], S42, 0xfe2ce6e0);
    II(c,d,a,b, x[6],  S43, 0xa3014314); II(b,c,d,a, x[13], S44, 0x4e0811a1);
    II(a,b,c,d, x[4],  S41, 0xf7537e82); II(d,a,b,c, x[11], S42, 0xbd3af235);
    II(c,d,a,b, x[2],  S43, 0x2ad7d2bb); II(b,c,d,a, x[9],  S44, 0xeb86d391);

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
}

void md5_update(Md5Ctx& ctx, const std::uint8_t* data, std::size_t len) {
    std::size_t index = static_cast<std::size_t>((ctx.bitcount >> 3) & 0x3F);
    ctx.bitcount += static_cast<std::uint64_t>(len) << 3;
    std::size_t part_len = 64 - index;
    std::size_t i = 0;
    if (len >= part_len) {
        std::memcpy(&ctx.buffer[index], data, part_len);
        md5_transform(ctx.state, ctx.buffer);
        for (i = part_len; i + 63 < len; i += 64) {
            md5_transform(ctx.state, &data[i]);
        }
        index = 0;
    }
    std::memcpy(&ctx.buffer[index], &data[i], len - i);
}

void md5_final(std::uint8_t digest[16], Md5Ctx& ctx) {
    std::uint8_t bits[8];
    for (int i = 0; i < 8; ++i) {
        bits[i] = static_cast<std::uint8_t>((ctx.bitcount >> (i * 8)) & 0xFF);
    }
    std::size_t index = static_cast<std::size_t>((ctx.bitcount >> 3) & 0x3F);
    std::size_t pad_len = (index < 56) ? (56 - index) : (120 - index);
    static const std::uint8_t padding[64] = {0x80};
    md5_update(ctx, padding, pad_len);
    md5_update(ctx, bits, 8);
    for (int i = 0; i < 4; ++i) {
        digest[i*4 + 0] = static_cast<std::uint8_t>(ctx.state[i] & 0xFF);
        digest[i*4 + 1] = static_cast<std::uint8_t>((ctx.state[i] >> 8) & 0xFF);
        digest[i*4 + 2] = static_cast<std::uint8_t>((ctx.state[i] >> 16) & 0xFF);
        digest[i*4 + 3] = static_cast<std::uint8_t>((ctx.state[i] >> 24) & 0xFF);
    }
}

} // anonymous

std::string md5_hex(const std::string& s) {
    Md5Ctx ctx;
    md5_init(ctx);
    md5_update(ctx, reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
    std::uint8_t digest[16];
    md5_final(digest, ctx);
    static const char hex[] = "0123456789abcdef";
    std::string out(32, '0');
    for (int i = 0; i < 16; ++i) {
        out[i*2]     = hex[(digest[i] >> 4) & 0xF];
        out[i*2 + 1] = hex[digest[i] & 0xF];
    }
    return out;
}

} // namespace audd::internal

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 AudD, LLC (https://audd.io)
//
// Official C++ SDK for the AudD music recognition API.
//
// Single-include umbrella header. Pull in `<audd/audd.hpp>` and you get the
// full public API: client, recognition models, error types, callback
// parsing, longpoll, and version info.
//
// API at a glance:
//
//     audd::AudD client("your-api-token");
//     auto r = client.recognize("https://audd.tech/example.mp3");
//     if (r) std::cout << r->artist << " - " << r->title << '\n';
//
// See README.md for full usage.

#ifndef AUDD_AUDD_HPP
#define AUDD_AUDD_HPP

#include <audd/advanced.hpp>
#include <audd/callback.hpp>
#include <audd/client.hpp>
#include <audd/custom_catalog.hpp>
#include <audd/error.hpp>
#include <audd/longpoll.hpp>
#include <audd/recognition.hpp>
#include <audd/source.hpp>
#include <audd/streams.hpp>
#include <audd/version.hpp>

#endif // AUDD_AUDD_HPP

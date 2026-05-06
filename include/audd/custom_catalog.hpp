// SPDX-License-Identifier: MIT
// Copyright (c) AudD <https://audd.io>

#ifndef AUDD_CUSTOM_CATALOG_HPP
#define AUDD_CUSTOM_CATALOG_HPP

#include <future>
#include <string>

#include <audd/source.hpp>

namespace audd {

class AudD; // forward

// CustomCatalogClient handles uploads to your private fingerprint catalog.
//
// **This is NOT how you submit audio for music recognition.** For
// recognition, use AudD::recognize (or AudD::recognize_enterprise for files
// longer than 25 seconds). This client manipulates your **private
// fingerprint catalog** so AudD's recognition can later identify *your own*
// tracks for *your account only*. Requires special access — contact
// api@audd.io if you need it enabled.
class CustomCatalogClient {
public:
    explicit CustomCatalogClient(AudD* parent) noexcept : parent_(parent) {}

    // add fingerprints `source` and stores it under the given audio_id slot.
    // Calling again with the same audio_id re-fingerprints that slot.
    // There is no public list/delete endpoint; track audio_id <-> song
    // mappings on your side.
    //
    // **Reminder: this is NOT for music recognition.**
    void add(int audio_id, const Source& source);
    std::future<void> add_async(int audio_id, Source source);

private:
    AudD* parent_;
};

} // namespace audd

#endif // AUDD_CUSTOM_CATALOG_HPP

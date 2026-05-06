// SPDX-License-Identifier: MIT
// Copyright (c) AudD <https://audd.io>

#include <audd/custom_catalog.hpp>

#include <stdexcept>
#include <string>

#include <audd/client.hpp>
#include <audd/error.hpp>

#include "internal/client_internal.hpp"
#include "internal/http_client.hpp"

namespace audd {

namespace {
constexpr const char* kApiBase = "https://api.audd.io";

// Local copy of the source-prepare logic from client.cpp. Kept private here
// to avoid exposing internal::FormFields machinery in the public API.
using SourceReopener = std::function<internal::FormFields()>;

SourceReopener prepare_source_for_upload(const Source& s) {
    return std::visit([](const auto& v) -> SourceReopener {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::string>) {
            const std::string& sv = v;
            if (sv.rfind("http://", 0) == 0 || sv.rfind("https://", 0) == 0) {
                std::string url = sv;
                return [url]() -> internal::FormFields {
                    internal::FormFields f; f.data["url"] = url; return f;
                };
            }
            FILE* fp = std::fopen(sv.c_str(), "rb");
            if (fp) {
                std::fclose(fp);
                std::string path = sv;
                return [path]() -> internal::FormFields {
                    internal::FormFields f;
                    internal::FileField file;
                    file.name = path.substr(path.find_last_of("/\\") + 1);
                    if (file.name.empty()) file.name = "upload.bin";
                    file.content_type = "application/octet-stream";
                    file.path = path;
                    f.file = std::move(file);
                    return f;
                };
            }
            throw std::invalid_argument("audd: source not URL nor existing file: " + sv);
        } else if constexpr (std::is_same_v<T, SourceUrl>) {
            std::string url = v.url;
            return [url]() -> internal::FormFields {
                internal::FormFields f; f.data["url"] = url; return f;
            };
        } else if constexpr (std::is_same_v<T, SourceFilePath>) {
            std::string path = v.path;
            return [path]() -> internal::FormFields {
                internal::FormFields f;
                internal::FileField file;
                file.name = path.substr(path.find_last_of("/\\") + 1);
                if (file.name.empty()) file.name = "upload.bin";
                file.content_type = "application/octet-stream";
                file.path = path;
                f.file = std::move(file);
                return f;
            };
        } else if constexpr (std::is_same_v<T, SourceBytes>) {
            SourceBytes copy = v;
            return [copy]() -> internal::FormFields {
                internal::FormFields f;
                internal::FileField file;
                file.name = copy.name;
                file.content_type = copy.mime_type;
                file.bytes = copy.bytes;
                f.file = std::move(file);
                return f;
            };
        }
    }, s);
}

} // anonymous

void CustomCatalogClient::add(int audio_id, const Source& source) {
    auto reopen = prepare_source_for_upload(source);
    auto fields = reopen();
    fields.data["audio_id"] = std::to_string(audio_id);
    parent_->internal()->post_form(std::string(kApiBase) + "/upload/", fields, /*custom_catalog_ctx*/ true);
}

std::future<void> CustomCatalogClient::add_async(int audio_id, Source source) {
    return std::async(std::launch::async, [this, audio_id, source = std::move(source)]() {
        this->add(audio_id, source);
    });
}

} // namespace audd

// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

// Native ESP-IDF HTTP OTA source — uses esp_http_client.h.
// Compiled when ENABLE_OTA_HTTP is defined on ESP-IDF builds.
#if defined(ENABLE_OTA_HTTP)

#include "../../core/i_ota_source.h"

namespace ungula::ota
{

/// OTA source that fetches version.txt and firmware binary over HTTP
/// using the ESP-IDF esp_http_client API.
class EspIdfHttpOtaSource : public IOtaSource {
    public:
        /// @param baseUrl     e.g. "http://updates.example.com/firmware/icb"
        /// @param binFilename e.g. "ICB.ino.bin"
        EspIdfHttpOtaSource(const char *baseUrl, const char *binFilename);

        bool fetchVersion(char *out, size_t maxLen) override;
        size_t getFirmwareSize() override;
        bool streamFirmware(OtaDataCallback callback, void *ctx) override;

    private:
        const char *baseUrl_;
        const char *binFilename_;
        size_t firmwareSize_ = 0;
};

// Backward-compatible alias
using IdfHttpOtaSource = EspIdfHttpOtaSource;

} // namespace ungula::ota
#endif // ENABLE_OTA_HTTP && ESP_PLATFORM

// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa

#pragma once

// ESP-IDF HTTP OTA source — uses esp_http_client directly.
// No Arduino dependency. Works with both HTTP and HTTPS (via CA bundle).

#if defined(ENABLE_OTA_HTTP) && defined(ESP_PLATFORM)

#include "../../../core/i_ota_source.h"

namespace ungula::ota {


/// OTA source that fetches version.txt and firmware binary over HTTP/HTTPS
/// using the ESP-IDF esp_http_client API (with TLS via the built-in CA bundle).
class EspHttpOtaSource : public IOtaSource {
    public:
        /// @param baseUrl     e.g. "https://updates.example.com/firmware/icb"
        /// @param binFilename e.g. "ICB.ino.bin"
        EspHttpOtaSource(const char* baseUrl, const char* binFilename);

        bool fetchVersion(char* out, size_t maxLen) override;
        size_t getFirmwareSize() override;
        bool streamFirmware(OtaDataCallback callback, void* ctx) override;

    private:
        const char* baseUrl_;
        const char* binFilename_;
        size_t firmwareSize_ = 0;
};

// Default alias — projects use HttpOtaSource and get the right impl
using HttpOtaSource = EspHttpOtaSource;

    }  // namespace ungula::ota
#endif  // ENABLE_OTA_HTTP && ESP_PLATFORM

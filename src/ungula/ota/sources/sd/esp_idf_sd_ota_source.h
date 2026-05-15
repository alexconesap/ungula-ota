// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

// Native ESP-IDF SD card OTA source — uses POSIX file I/O via VFS.
// Requires the SD card to be mounted via sdmmc_host + esp_vfs_fat before use.
// Compiled when ENABLE_OTA_SD is defined on ESP-IDF builds.
#if defined(ENABLE_OTA_SD) && defined(ESP_PLATFORM)

#include "../../core/i_ota_source.h"

namespace ungula::ota
{

/// OTA source that reads version.txt and firmware binary from an SD card
/// using POSIX file I/O (the card must be mounted via VFS beforehand).
class EspIdfSdOtaSource : public IOtaSource {
    public:
        /// @param basePath     VFS mount path + subdir, e.g. "/sdcard/firmware/icb"
        /// @param binFilename  e.g. "ICB.ino.bin"
        EspIdfSdOtaSource(const char *basePath, const char *binFilename);

        bool fetchVersion(char *out, size_t maxLen) override;
        size_t getFirmwareSize() override;
        bool streamFirmware(OtaDataCallback callback, void *ctx) override;

    private:
        const char *basePath_;
        const char *binFilename_;
        size_t firmwareSize_ = 0;
};

// Backward-compatible alias
using IdfSdOtaSource = EspIdfSdOtaSource;

} // namespace ungula::ota
#endif // ENABLE_OTA_SD && ESP_PLATFORM

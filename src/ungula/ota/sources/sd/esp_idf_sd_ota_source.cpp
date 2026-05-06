// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#if defined(ENABLE_OTA_SD) && defined(ESP_PLATFORM)

#include "esp_idf_sd_ota_source.h"

#include <emblogx/logger.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstring>

namespace ungula::ota {


static constexpr size_t STREAM_BUF_SIZE = 4096;

EspIdfSdOtaSource::EspIdfSdOtaSource(const char* basePath, const char* binFilename)
    : basePath_(basePath), binFilename_(binFilename) {}

bool EspIdfSdOtaSource::fetchVersion(char* out, size_t maxLen) {
    char path[128];
    snprintf(path, sizeof(path), "%s/version.txt", basePath_);

    FILE* f = fopen(path, "r");
    if (!f) {
        log_error("OTA SD: cannot open %s", path);
        return false;
    }

    if (!fgets(out, static_cast<int>(maxLen), f)) {
        log_error("OTA SD: version.txt is empty");
        fclose(f);
        return false;
    }
    fclose(f);

    // Trim trailing whitespace and newlines
    size_t len = strlen(out);
    while (len > 0 &&
           (out[len - 1] == '\n' || out[len - 1] == '\r' || out[len - 1] == ' ')) {
        out[--len] = '\0';
    }

    if (len == 0) {
        log_error("OTA SD: version.txt is empty after trim");
        return false;
    }

    // Probe firmware binary size so getFirmwareSize() is available
    // before streamFirmware() is called.
    char binPath[128];
    snprintf(binPath, sizeof(binPath), "%s/%s", basePath_, binFilename_);
    struct stat binSt;
    if (stat(binPath, &binSt) == 0 && binSt.st_size > 0) {
        firmwareSize_ = static_cast<size_t>(binSt.st_size);
    }

    return true;
}

size_t EspIdfSdOtaSource::getFirmwareSize() {
    return firmwareSize_;
}

bool EspIdfSdOtaSource::streamFirmware(OtaDataCallback callback, void* ctx) {
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", basePath_, binFilename_);

    // Get file size
    struct stat st;
    if (stat(path, &st) != 0 || st.st_size <= 0) {
        log_error("OTA SD: cannot stat %s or file is empty", path);
        return false;
    }

    firmwareSize_ = static_cast<size_t>(st.st_size);

    FILE* f = fopen(path, "rb");
    if (!f) {
        log_error("OTA SD: cannot open %s", path);
        return false;
    }

    uint8_t buf[STREAM_BUF_SIZE];
    size_t remaining = firmwareSize_;

    while (remaining > 0) {
        size_t toRead = (remaining < STREAM_BUF_SIZE) ? remaining : STREAM_BUF_SIZE;
        size_t bytesRead = fread(buf, 1, toRead, f);

        if (bytesRead == 0) {
            log_error("OTA SD: read failed (%u bytes remaining)", (unsigned)remaining);
            fclose(f);
            return false;
        }

        if (!callback(buf, bytesRead, ctx)) {
            log_warn("OTA SD: callback aborted transfer");
            fclose(f);
            return false;
        }

        remaining -= bytesRead;
    }

    fclose(f);
    return true;
}

    }  // namespace ungula::ota
#endif  // ENABLE_OTA_SD && ESP_PLATFORM

// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#if defined(ENABLE_OTA_HTTP)

#include "esp_idf_http_ota_source.h"

#include <emblogx/logger.h>
#include <esp_http_client.h>

#include <cstring>

namespace ungula::ota {


static constexpr size_t STREAM_BUF_SIZE = 4096;

EspIdfHttpOtaSource::EspIdfHttpOtaSource(const char* baseUrl, const char* binFilename)
    : baseUrl_(baseUrl), binFilename_(binFilename) {}

bool EspIdfHttpOtaSource::fetchVersion(char* out, size_t maxLen) {
    char url[256];
    snprintf(url, sizeof(url), "%s/version.txt", baseUrl_);

    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 10000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        log_error("OTA HTTP: failed to init HTTP client");
        return false;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        log_error("OTA HTTP: GET %s failed: %s", url, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int contentLength = esp_http_client_fetch_headers(client);
    int statusCode = esp_http_client_get_status_code(client);
    if (statusCode != 200) {
        log_error("OTA HTTP: GET %s returned HTTP %d", url, statusCode);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    // Read the version string (small payload, read in one go)
    size_t readMax = (contentLength > 0 && (size_t)contentLength < maxLen - 1)
                             ? (size_t)contentLength
                             : maxLen - 1;
    int bytesRead = esp_http_client_read(client, out, readMax);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (bytesRead <= 0) {
        log_error("OTA HTTP: version.txt read returned %d bytes", bytesRead);
        return false;
    }

    out[bytesRead] = '\0';

    // Trim trailing whitespace and newlines
    while (bytesRead > 0 && (out[bytesRead - 1] == '\n' || out[bytesRead - 1] == '\r' ||
                             out[bytesRead - 1] == ' ')) {
        out[--bytesRead] = '\0';
    }

    if (bytesRead == 0) {
        log_error("OTA HTTP: version.txt is empty after trim");
        return false;
    }

    // Probe firmware binary size via HEAD request so getFirmwareSize()
    // is available before streamFirmware() is called.
    char binUrl[256];
    snprintf(binUrl, sizeof(binUrl), "%s/%s", baseUrl_, binFilename_);

    esp_http_client_config_t headCfg = {};
    headCfg.url = binUrl;
    headCfg.timeout_ms = 10000;
    headCfg.method = HTTP_METHOD_HEAD;

    esp_http_client_handle_t headClient = esp_http_client_init(&headCfg);
    if (headClient) {
        esp_err_t headErr = esp_http_client_open(headClient, 0);
        if (headErr == ESP_OK) {
            int cl = esp_http_client_fetch_headers(headClient);
            int headStatus = esp_http_client_get_status_code(headClient);
            if (headStatus == 200 && cl > 0) {
                firmwareSize_ = static_cast<size_t>(cl);
            }
            esp_http_client_close(headClient);
        }
        esp_http_client_cleanup(headClient);
    }

    return true;
}

size_t EspIdfHttpOtaSource::getFirmwareSize() {
    return firmwareSize_;
}

bool EspIdfHttpOtaSource::streamFirmware(OtaDataCallback callback, void* ctx) {
    char url[256];
    snprintf(url, sizeof(url), "%s/%s", baseUrl_, binFilename_);

    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 30000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        log_error("OTA HTTP: failed to init HTTP client");
        return false;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        log_error("OTA HTTP: GET %s failed: %s", url, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int contentLength = esp_http_client_fetch_headers(client);
    int statusCode = esp_http_client_get_status_code(client);
    if (statusCode != 200) {
        log_error("OTA HTTP: GET %s returned HTTP %d", url, statusCode);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    if (contentLength <= 0) {
        log_error("OTA HTTP: invalid content length");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    firmwareSize_ = static_cast<size_t>(contentLength);

    uint8_t buf[STREAM_BUF_SIZE];
    size_t remaining = firmwareSize_;

    while (remaining > 0) {
        size_t toRead = (remaining < STREAM_BUF_SIZE) ? remaining : STREAM_BUF_SIZE;
        int bytesRead = esp_http_client_read(client, reinterpret_cast<char*>(buf), toRead);

        if (bytesRead <= 0) {
            log_error("OTA HTTP: stream read failed (%u bytes remaining)",
                      (unsigned)remaining);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
        }

        if (!callback(buf, static_cast<size_t>(bytesRead), ctx)) {
            log_warn("OTA HTTP: callback aborted transfer");
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
        }

        remaining -= static_cast<size_t>(bytesRead);
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return true;
}

    }  // namespace ungula::ota
#endif  // ENABLE_OTA_HTTP && ESP_PLATFORM

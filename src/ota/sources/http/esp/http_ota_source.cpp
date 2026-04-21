// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa

// ESP-IDF HTTP OTA source — pure ESP-IDF, no Arduino dependency.
// Uses esp_http_client with esp_crt_bundle for HTTPS support.

#if defined(ENABLE_OTA_HTTP) && defined(ESP_PLATFORM)

#include "http_ota_source.h"

#include <emblogx/logger.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <wifi/wifi_sta.h>

#include <cstdio>
#include <cstring>

namespace ungula {
    namespace ota {

        static constexpr size_t STREAM_BUF_SIZE = 4096;
        static constexpr int HTTP_TIMEOUT_MS = 30000;

        EspHttpOtaSource::EspHttpOtaSource(const char* baseUrl, const char* binFilename)
            : baseUrl_(baseUrl), binFilename_(binFilename) {}

        // -- fetchVersion: GET {baseUrl}/version.txt --

        /// Collect response body into a char buffer (for version.txt)
        struct VersionCtx {
                char* buf;
                size_t maxLen;
                size_t written;
        };

        static esp_err_t version_event_handler(esp_http_client_event_t* evt) {
            if (evt->event_id == HTTP_EVENT_ON_DATA) {
                auto* ctx = static_cast<VersionCtx*>(evt->user_data);
                size_t space = ctx->maxLen - ctx->written - 1;
                if (space > 0) {
                    size_t copy =
                            evt->data_len < space ? static_cast<size_t>(evt->data_len) : space;
                    std::memcpy(ctx->buf + ctx->written, evt->data, copy);
                    ctx->written += copy;
                    ctx->buf[ctx->written] = '\0';
                }
            }
            return ESP_OK;
        }

        bool EspHttpOtaSource::fetchVersion(char* out, size_t maxLen) {
            if (!ungula::wifi::sta_is_connected()) {
                log_error("OTA HTTP: WiFi not connected");
                return false;
            }

            char url[256];
            std::snprintf(url, sizeof(url), "%s/version.txt", baseUrl_);

            VersionCtx ctx = {out, maxLen, 0};

            esp_http_client_config_t config = {};
            config.url = url;
            config.method = HTTP_METHOD_GET;
            config.timeout_ms = HTTP_TIMEOUT_MS;
            config.event_handler = version_event_handler;
            config.user_data = &ctx;
            config.crt_bundle_attach = esp_crt_bundle_attach;

            esp_http_client_handle_t client = esp_http_client_init(&config);
            if (!client) {
                log_error("OTA HTTP: failed to init client for %s", url);
                return false;
            }

            esp_err_t err = esp_http_client_perform(client);
            int status = esp_http_client_get_status_code(client);
            esp_http_client_cleanup(client);

            if (err != ESP_OK || status != 200) {
                log_error("OTA HTTP: GET %s failed (err=%s, status=%d)", url, esp_err_to_name(err),
                          status);
                return false;
            }

            // Trim trailing whitespace/newlines
            while (ctx.written > 0 &&
                   (out[ctx.written - 1] == '\n' || out[ctx.written - 1] == '\r' ||
                    out[ctx.written - 1] == ' ')) {
                ctx.written--;
                out[ctx.written] = '\0';
            }

            if (ctx.written == 0) {
                log_error("OTA HTTP: version string empty");
                return false;
            }

            return true;
        }

        size_t EspHttpOtaSource::getFirmwareSize() {
            return firmwareSize_;
        }

        // -- streamFirmware: GET {baseUrl}/{binFilename}, stream chunks via callback --

        bool EspHttpOtaSource::streamFirmware(OtaDataCallback callback, void* ctx) {
            if (!ungula::wifi::sta_is_connected()) {
                log_error("OTA HTTP: WiFi not connected");
                return false;
            }

            char url[256];
            std::snprintf(url, sizeof(url), "%s/%s", baseUrl_, binFilename_);

            esp_http_client_config_t config = {};
            config.url = url;
            config.method = HTTP_METHOD_GET;
            config.timeout_ms = HTTP_TIMEOUT_MS;
            config.buffer_size = STREAM_BUF_SIZE;
            config.crt_bundle_attach = esp_crt_bundle_attach;
            // No event handler — we'll read manually for streaming

            esp_http_client_handle_t client = esp_http_client_init(&config);
            if (!client) {
                log_error("OTA HTTP: failed to init client for %s", url);
                return false;
            }

            // Open the connection and send the request
            esp_err_t err = esp_http_client_open(client, 0);  // 0 = no request body
            if (err != ESP_OK) {
                log_error("OTA HTTP: open failed: %s", esp_err_to_name(err));
                esp_http_client_cleanup(client);
                return false;
            }

            // Read response headers
            int contentLength = esp_http_client_fetch_headers(client);
            int status = esp_http_client_get_status_code(client);

            if (status != 200) {
                log_error("OTA HTTP: GET %s returned %d", url, status);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return false;
            }

            if (contentLength <= 0) {
                log_error("OTA HTTP: invalid content length %d", contentLength);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return false;
            }

            firmwareSize_ = static_cast<size_t>(contentLength);
            log_info("OTA HTTP: firmware size %u bytes", static_cast<unsigned>(firmwareSize_));

            // Stream the body in chunks
            uint8_t buf[STREAM_BUF_SIZE];
            size_t remaining = firmwareSize_;
            bool success = true;

            while (remaining > 0) {
                int toRead = (remaining < STREAM_BUF_SIZE) ? static_cast<int>(remaining)
                                                           : static_cast<int>(STREAM_BUF_SIZE);

                int bytesRead = esp_http_client_read(client, reinterpret_cast<char*>(buf), toRead);
                if (bytesRead <= 0) {
                    log_error("OTA HTTP: read failed (%u bytes remaining)",
                              static_cast<unsigned>(remaining));
                    success = false;
                    break;
                }

                if (!callback(buf, static_cast<size_t>(bytesRead), ctx)) {
                    log_warn("OTA HTTP: callback aborted transfer");
                    success = false;
                    break;
                }

                remaining -= static_cast<size_t>(bytesRead);
            }

            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return success;
        }

    }  // namespace ota
}  // namespace ungula

#endif  // ENABLE_OTA_HTTP && ESP_PLATFORM

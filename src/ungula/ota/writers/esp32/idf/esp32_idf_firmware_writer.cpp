// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#if defined(ESP_PLATFORM)

#include "esp32_idf_firmware_writer.h"

#include <emblogx/logger.h>
#include <esp_ota_ops.h>

namespace ungula::ota
{

bool Esp32IdfFirmwareWriter::begin(size_t totalSize)
{
        partition_ = esp_ota_get_next_update_partition(nullptr);
        if (!partition_) {
                log_error("OTA Writer: no OTA partition available");
                return false;
        }

        // OTA_SIZE_UNKNOWN tells ESP-IDF to accept data without a pre-declared
        // size. Passing literal 0 can fail on some IDF versions.
        size_t sizeArg = (totalSize > 0) ? totalSize : OTA_SIZE_UNKNOWN;
        esp_err_t err = esp_ota_begin(partition_, sizeArg, &handle_);
        if (err != ESP_OK) {
                log_error("OTA Writer: esp_ota_begin failed: %s", esp_err_to_name(err));
                partition_ = nullptr;
                return false;
        }

        return true;
}

size_t Esp32IdfFirmwareWriter::writeChunk(const uint8_t *data, size_t len)
{
        esp_err_t err = esp_ota_write(handle_, data, len);
        if (err != ESP_OK) {
                log_error("OTA Writer: esp_ota_write failed: %s", esp_err_to_name(err));
                return 0;
        }
        return len;
}

bool Esp32IdfFirmwareWriter::end()
{
        esp_err_t err = esp_ota_end(handle_);
        if (err != ESP_OK) {
                log_error("OTA Writer: esp_ota_end failed: %s", esp_err_to_name(err));
                return false;
        }

        err = esp_ota_set_boot_partition(partition_);
        if (err != ESP_OK) {
                log_error("OTA Writer: esp_ota_set_boot_partition failed: %s",
                          esp_err_to_name(err));
                return false;
        }

        return true;
}

void Esp32IdfFirmwareWriter::abort()
{
        esp_ota_abort(handle_);
        handle_ = 0;
        partition_ = nullptr;
        log_warn("OTA Writer: aborted");
}

} // namespace ungula::ota
#endif // ESP_PLATFORM

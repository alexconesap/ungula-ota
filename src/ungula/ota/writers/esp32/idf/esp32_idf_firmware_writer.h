// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

// Native ESP-IDF firmware writer — uses esp_ota_ops.h directly.
// Compiled on ESP-IDF builds.
#if defined(ESP_PLATFORM)

#include <esp_ota_ops.h>

#include "../../../core/i_firmware_writer.h"

namespace ungula::ota {


/// ESP-IDF native firmware writer using the OTA partition API.
class Esp32IdfFirmwareWriter : public IFirmwareWriter {
    public:
        bool begin(size_t totalSize) override;
        size_t writeChunk(const uint8_t* data, size_t len) override;
        bool end() override;
        void abort() override;

    private:
        esp_ota_handle_t handle_ = 0;
        const esp_partition_t* partition_ = nullptr;
};

using Esp32FirmwareWriter = Esp32IdfFirmwareWriter;

    }  // namespace ungula::ota
#endif  // ESP_PLATFORM

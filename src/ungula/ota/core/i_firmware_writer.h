// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include <cstddef>
#include <cstdint>

namespace ungula::ota
{

/// Interface for writing firmware to flash (platform-specific).
class IFirmwareWriter {
    public:
        virtual ~IFirmwareWriter() = default;

        /// Begin a firmware write session for the given total size.
        /// A totalSize of 0 means the size is unknown — implementations must
        /// handle this by using the platform's unknown-size constant.
        /// Returns true if the device is ready to receive firmware data.
        virtual bool begin(size_t totalSize) = 0;

        /// Write a chunk of firmware data. Returns number of bytes written.
        virtual size_t writeChunk(const uint8_t *data, size_t len) = 0;

        /// Finalize the write. Returns true if the firmware image is valid.
        virtual bool end() = 0;

        /// Abort an in-progress write and clean up.
        virtual void abort() = 0;
};

} // namespace ungula::ota

// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include <cstddef>
#include <cstdint>

namespace ungula::ota {

    /// Callback invoked by IOtaSource::streamFirmware() for each data chunk.
    /// Returns true to continue streaming, false to abort.
    /// @param data  Pointer to the chunk bytes
    /// @param len   Number of bytes in this chunk
    /// @param ctx   Opaque context pointer passed through from streamFirmware()
    using OtaDataCallback = bool (*)(const uint8_t* data, size_t len, void* ctx);

    /// Interface for OTA firmware sources (HTTP server, SD card, etc.)
    class IOtaSource {
        public:
            virtual ~IOtaSource() = default;

            /// Fetch the remote version string into the provided buffer.
            /// Returns true on success.
            virtual bool fetchVersion(char* out, size_t maxLen) = 0;

            /// Get the firmware binary size in bytes.
            /// For HTTP sources this is only valid after streamFirmware() has
            /// received the response headers. SD/file sources set it in fetchVersion().
            /// Returns 0 if not yet known.
            virtual size_t getFirmwareSize() = 0;

            /// Stream the firmware binary in chunks via the callback.
            /// Implementations must set the firmware size (readable via getFirmwareSize())
            /// before invoking the first data callback.
            /// Returns true if all data was delivered successfully.
            virtual bool streamFirmware(OtaDataCallback callback, void* ctx) = 0;
    };

}  // namespace ungula::ota

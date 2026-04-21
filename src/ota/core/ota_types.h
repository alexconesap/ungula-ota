// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include <cstddef>
#include <cstdint>

namespace ungula {
    namespace ota {

        struct OtaProgressCallbackData {
                size_t bytesWritten;
                size_t totalBytes;
        };

        /// Status codes returned by OtaUpdater operations
        enum class OtaStatus : uint8_t {
            Ok = 0,              // Operation succeeded (update available / update complete)
            NoUpdate,            // Current version is up to date
            NoSource,            // No IOtaSource was injected
            NoWriter,            // No IFirmwareWriter was injected
            FetchVersionFailed,  // Could not retrieve remote version
            VersionParseFailed,  // Remote version string was empty or invalid
            BeginFailed,         // Writer failed to start (not enough space, etc.)
            StreamFailed,        // Firmware download/read failed mid-stream
            WriteFailed,         // Writer rejected a chunk
            FinalizeFailed,      // Writer::end() returned false
        };

        /// Convert OtaStatus to a short string label for logging
        const char* otaStatusToString(OtaStatus status);

        /// Callback for progress reporting: (bytesWritten, totalBytes)
        using OtaProgressCallback = void (*)(OtaProgressCallbackData data);

    }  // namespace ota
}  // namespace ungula

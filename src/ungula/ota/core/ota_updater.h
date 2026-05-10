// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include "ota_types.h"

namespace ungula::ota
{

    class IOtaSource;
    class IFirmwareWriter;

    /// Facade for performing OTA firmware updates.
    /// Owns nothing — caller manages the lifetime of source and writer.
    class OtaUpdater {
    public:
        OtaUpdater() = default;

        /// Inject the firmware source (HTTP, SD, etc.)
        void setSource(IOtaSource *source);

        /// Inject the firmware writer (ESP32, etc.)
        void setWriter(IFirmwareWriter *writer);

        /// Set an optional progress callback
        void setProgressCallback(OtaProgressCallback callback);

        /// Check if a newer version is available.
        /// Returns OtaStatus::Ok if an update is available, OtaStatus::NoUpdate if current.
        OtaStatus checkForUpdate(const char *currentVersion);

        /// Check for update and apply it if available.
        /// If autoReboot is true and the update succeeds, the device reboots automatically.
        /// Returns OtaStatus::Ok on success (if autoReboot=false), or an error status.
        OtaStatus performUpdate(const char *currentVersion, bool autoReboot = true);

        /// Download and install the firmware binary (skip version check).
        /// Call this after checkForUpdate() returned OtaStatus::Ok.
        /// The writer's begin() is deferred until the firmware size is known
        /// from the download response headers, which fixes the incorrect
        /// UPDATE_SIZE_UNKNOWN that caused "premature end" failures.
        OtaStatus downloadAndInstall(bool autoReboot = true);

        /// Remote version string from the last checkForUpdate() call.
        /// Valid after checkForUpdate() returns Ok or NoUpdate.
        const char *getRemoteVersion() const
        {
            return remoteVersion_;
        }

    private:
        IOtaSource *source_ = nullptr;
        IFirmwareWriter *writer_ = nullptr;
        OtaProgressCallback progressCb_ = nullptr;

        // Cached remote version from last checkForUpdate / performUpdate
        char remoteVersion_[32] = {};
    };

} // namespace ungula::ota

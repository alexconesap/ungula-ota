// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#include "ota_updater.h"

#include <emblogx/logger.h>
#include <system/system_reboot.h>

#include <cstring>

#include "i_firmware_writer.h"
#include "i_ota_source.h"
#include "ota_version.h"

namespace ungula {
    namespace ota {

        const char* otaStatusToString(OtaStatus status) {
            switch (status) {
                case OtaStatus::Ok:
                    return "ok";
                case OtaStatus::NoUpdate:
                    return "no_update";
                case OtaStatus::NoSource:
                    return "no_source";
                case OtaStatus::NoWriter:
                    return "no_writer";
                case OtaStatus::FetchVersionFailed:
                    return "fetch_version_failed";
                case OtaStatus::VersionParseFailed:
                    return "version_parse_failed";
                case OtaStatus::BeginFailed:
                    return "begin_failed";
                case OtaStatus::StreamFailed:
                    return "stream_failed";
                case OtaStatus::WriteFailed:
                    return "write_failed";
                case OtaStatus::FinalizeFailed:
                    return "finalize_failed";
            }
            return "unknown";
        }

        void OtaUpdater::setSource(IOtaSource* source) {
            source_ = source;
        }

        void OtaUpdater::setWriter(IFirmwareWriter* writer) {
            writer_ = writer;
        }

        void OtaUpdater::setProgressCallback(OtaProgressCallback callback) {
            progressCb_ = callback;
        }

        OtaStatus OtaUpdater::checkForUpdate(const char* currentVersion) {
            if (source_ == nullptr) {
                log_error("OTA: no source configured");
                return OtaStatus::NoSource;
            }

            memset(remoteVersion_, 0, sizeof(remoteVersion_));
            if (!source_->fetchVersion(remoteVersion_, sizeof(remoteVersion_))) {
                log_error("OTA: failed to fetch remote version");
                return OtaStatus::FetchVersionFailed;
            }

            if (strlen(remoteVersion_) == 0) {
                log_error("OTA: remote version string is empty");
                return OtaStatus::VersionParseFailed;
            }

            int cmp = compareVersions(remoteVersion_, currentVersion);
            if (cmp > 0) {
                return OtaStatus::Ok;
            }

            return OtaStatus::NoUpdate;
        }

        /// Context passed through the stream callback function pointer.
        /// The writer's begin() is called lazily on the first data chunk so
        /// that the firmware size from Content-Length is already available.
        struct StreamCtx {
                IFirmwareWriter* writer;
                IOtaSource* source;
                OtaProgressCallback progressCb;
                size_t totalSize;
                size_t totalWritten;
                bool writeError;
                bool beginError;
                bool beginCalled;
        };

        static bool streamCallback(const uint8_t* data, size_t len, void* ctx) {
            auto* context = static_cast<StreamCtx*>(ctx);

            // Lazy begin: the source has set the firmware size from the HTTP
            // Content-Length header before delivering the first data chunk.
            if (!context->beginCalled) {
                context->totalSize = context->source->getFirmwareSize();
                if (!context->writer->begin(context->totalSize)) {
                    log_error("OTA: writer begin() failed (not enough space?)");
                    context->beginError = true;
                    return false;
                }
                context->beginCalled = true;
            }

            size_t written = context->writer->writeChunk(data, len);
            if (written != len) {
                log_error("OTA: write chunk failed (%u/%u bytes)", static_cast<unsigned>(written),
                          static_cast<unsigned>(len));
                context->writeError = true;
                return false;
            }
            context->totalWritten += written;
            if (context->progressCb != nullptr) {
                context->progressCb(
                        OtaProgressCallbackData{context->totalWritten, context->totalSize});
            }
            return true;
        }

        OtaStatus OtaUpdater::downloadAndInstall(bool autoReboot) {
            if (source_ == nullptr) {
                log_error("OTA: no source configured");
                return OtaStatus::NoSource;
            }
            if (writer_ == nullptr) {
                log_error("OTA: no firmware writer configured");
                return OtaStatus::NoWriter;
            }

            // begin() is NOT called here — it is deferred to the first stream
            // callback so that the correct firmware size from Content-Length is
            // available. Calling begin(0) would pass UPDATE_SIZE_UNKNOWN to the
            // ESP32 Update library, causing "premature end" on finalize.

            StreamCtx sctx = {writer_, source_, progressCb_, 0, 0, false, false, false};

            bool streamOk = source_->streamFirmware(streamCallback, &sctx);

            if (!streamOk || sctx.writeError || sctx.beginError) {
                log_error("OTA: stream/write failed at %u bytes",
                          static_cast<unsigned>(sctx.totalWritten));
                if (sctx.beginCalled) {
                    writer_->abort();
                }
                if (sctx.beginError)
                    return OtaStatus::BeginFailed;
                if (sctx.writeError)
                    return OtaStatus::WriteFailed;
                return OtaStatus::StreamFailed;
            }

            if (!sctx.beginCalled) {
                log_error("OTA: stream delivered no data");
                return OtaStatus::StreamFailed;
            }

            if (!writer_->end()) {
                log_error("OTA: finalize failed");
                return OtaStatus::FinalizeFailed;
            }

            if (autoReboot) {
                ungula::SystemControl::rebootAfterMs(500);
            }

            return OtaStatus::Ok;
        }

        OtaStatus OtaUpdater::performUpdate(const char* currentVersion, bool autoReboot) {
            OtaStatus checkResult = checkForUpdate(currentVersion);
            if (checkResult != OtaStatus::Ok) {
                return checkResult;
            }
            return downloadAndInstall(autoReboot);
        }

    }  // namespace ota
}  // namespace ungula

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alex Conesa

#include "ungula/ota/coordinator/ota_coordinator.h"

#include <emblogx/logger.h>

#include <ungula/core/system/system_reboot.h>
#include <ungula/core/time/time.h>
#include <ungula/ota/core/ota_types.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdio>
#include <cstring>

namespace ungula::ota
{

const char *coordinatorPhaseToString(CoordinatorPhase phase)
{
        switch (phase) {
        case CoordinatorPhase::Idle:
                return "IDLE";
        case CoordinatorPhase::SendingStart:
                return "SENDING_START";
        case CoordinatorPhase::WaitingStartAcks:
                return "WAITING_START_ACKS";
        case CoordinatorPhase::WaitingPeers:
                return "WAITING_PEERS";
        case CoordinatorPhase::UpdatingMain:
                return "UPDATING_MAIN";
        case CoordinatorPhase::Complete:
                return "COMPLETE";
        case CoordinatorPhase::Failed:
                return "FAILED";
        default:
                return "UNKNOWN";
        }
}

OtaCoordinator::OtaCoordinator(OtaUpdater &updater, CoordinatorHost &host)
        : updater_(updater)
        , host_(host)
{
}

bool OtaCoordinator::start()
{
        if (isActive()) {
                log_warn("OTA: already in progress");
                return false;
        }

        update_applied_ = false;
        log_info("OTA: starting update sequence");

        if (!host_.hasConnectedPeers()) {
                log_info("OTA: no peers connected, skipping their update");
                notifyStep(1, OtaStep::SkippingPeers);
                startUpdateTask();
                return true;
        }

        notifyStep(1, OtaStep::NotifyingPeers);
        host_.sendOtaStartToPeers();
        advancePhase(CoordinatorPhase::WaitingStartAcks);
        return true;
}

void OtaCoordinator::loop(uint32_t now_ms)
{
        if (!isActive()) {
                return;
        }

        uint32_t elapsed = now_ms - phase_start_ms_;

        switch (phase_) {
        case CoordinatorPhase::WaitingStartAcks:
                if (host_.allPeersAcked()) {
                        log_info("OTA: all peers acknowledged, waiting for them to update...");
                        notifyStep(2, OtaStep::WaitingPeers);
                        advancePhase(CoordinatorPhase::WaitingPeers);
                } else if (elapsed > ACK_TIMEOUT_MS) {
                        log_warn("OTA: start ACK timeout, proceeding");
                        notifyStep(2, OtaStep::WaitingPeers);
                        advancePhase(CoordinatorPhase::WaitingPeers);
                }
                break;

        case CoordinatorPhase::WaitingPeers:
                if ((host_.allPeersUpdated() && elapsed > PEER_SETTLE_MS) || elapsed > PEER_UPDATE_TIMEOUT_MS) {
                        log_info("OTA: peers ready (or timed out), proceeding to MAIN update");
                        startUpdateTask();
                }
                break;

        case CoordinatorPhase::UpdatingMain:
                if (ota_task_done_) {
                        if (ota_task_success_) {
                                update_applied_ = true;
                                log_info("OTA: MAIN update successful, rebooting");
                                notifyResult(true, OtaResultKind::Installed, nullptr);
                                setStatus("Update complete, rebooting...");
                                advancePhase(CoordinatorPhase::Complete);
                                ungula::core::system::SystemControl::rebootAfterMs(2000);
                        } else {
                                fail(ota_task_error_);
                        }
                        ota_task_handle_ = nullptr;
                }
                break;

        default:
                break;
        }
}

void OtaCoordinator::startUpdateTask()
{
        notifyStep(3, OtaStep::Checking);
        const char *version = host_.currentFirmwareVersion();
        auto checkResult = updater_.checkForUpdate(version);
        log_info("OTA: current=%s, remote=%s", version, updater_.getRemoteVersion());

        if (checkResult == OtaStatus::NoUpdate) {
                log_info("OTA: no update available");
                notifyResult(true, OtaResultKind::UpToDate, nullptr);
                advancePhase(CoordinatorPhase::Complete);
                return;
        }

        if (checkResult != OtaStatus::Ok) {
                char buf[80];
                snprintf(buf, sizeof(buf), "Update check failed: %s", otaStatusToString(checkResult));
                fail(buf);
                return;
        }

        log_info("OTA: update available (%s -> %s)", version, updater_.getRemoteVersion());

        notifyStep(4, OtaStep::Downloading);
        setStatus("Downloading firmware...");

        download_percent_ = 0;
        ota_task_done_ = false;
        ota_task_success_ = false;
        ota_task_error_[0] = '\0';
        advancePhase(CoordinatorPhase::UpdatingMain);

        xTaskCreatePinnedToCore(otaTaskFunc, "ota_dl", OTA_TASK_STACK_SIZE, this, 1,
                                reinterpret_cast<TaskHandle_t *>(&ota_task_handle_), 1);
}

// Static pointer so the non-capturing progress lambda can reach the percent.
static volatile int *s_downloadPercentPtr = nullptr;

void OtaCoordinator::otaTaskFunc(void *param)
{
        auto *self = static_cast<OtaCoordinator *>(param);
        s_downloadPercentPtr = &self->download_percent_;

        self->updater_.setProgressCallback([](OtaProgressCallbackData data) {
                if (data.totalBytes > 0) {
                        int percent = static_cast<int>((data.bytesWritten * 100) / data.totalBytes);
                        if (s_downloadPercentPtr != nullptr) {
                                *s_downloadPercentPtr = percent;
                        }
                        static int lastLoggedPct = -1;
                        if ((percent / 10) != (lastLoggedPct / 10)) {
                                lastLoggedPct = percent;
                                log_info("OTA: %d%% (%u / %u bytes)", percent, static_cast<unsigned>(data.bytesWritten),
                                         static_cast<unsigned>(data.totalBytes));
                        }
                }
        });

        auto updateResult = self->updater_.downloadAndInstall(false);

        if (updateResult == OtaStatus::Ok) {
                self->ota_task_success_ = true;
        } else {
                self->ota_task_success_ = false;
                snprintf(self->ota_task_error_, sizeof(self->ota_task_error_), "Update failed: %s",
                         otaStatusToString(updateResult));
        }

        self->ota_task_done_ = true;
        vTaskDelete(nullptr);
}

void OtaCoordinator::advancePhase(CoordinatorPhase next)
{
        log_info("OTA: phase %s -> %s", coordinatorPhaseToString(phase_), coordinatorPhaseToString(next));
        phase_ = next;
        phase_start_ms_ = ungula::core::time::millis();
}

void OtaCoordinator::setStatus(const char *msg)
{
        std::strncpy(status_msg_, msg, sizeof(status_msg_) - 1);
        status_msg_[sizeof(status_msg_) - 1] = '\0';
        log_info("OTA status: %s", status_msg_);
}

void OtaCoordinator::fail(const char *reason)
{
        log_error("OTA: FAILED — %s", reason);
        std::strncpy(status_msg_, reason, sizeof(status_msg_) - 1);
        status_msg_[sizeof(status_msg_) - 1] = '\0';
        phase_ = CoordinatorPhase::Failed;
        notifyResult(false, OtaResultKind::Failed, reason);
}

} // namespace ungula::ota

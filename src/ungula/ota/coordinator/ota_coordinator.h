// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alex Conesa

#pragma once

// ---------------------------------------------------------------
// OtaCoordinator — drives a multi-node OTA update sequence:
//
//   1. Tell the peer nodes to update first (host-specific protocol)
//   2. Wait for the peers to update + reconnect
//   3. MAIN checks + downloads + flashes its own firmware (via OtaUpdater)
//   4. Reboot if updated, or report "up to date"
//
// The FSM, the MAIN update, the background download task and the reboot are
// generic and live here. The PROJECT-specific bits — how to reach the peers,
// their protocol, their identities, the firmware version — are abstracted
// behind CoordinatorHost so the coordinator carries no node-protocol knowledge
// and no UI-string dependency (steps/results are reported as enums; the host
// maps them to display text).
//
// ESP32 only (uses a FreeRTOS task for the download).
// ---------------------------------------------------------------

#include <cstdint>

#include "ungula/ota/core/ota_updater.h"
#include "ungula/core/time/time.h"

namespace ungula::ota
{

enum class CoordinatorPhase : uint8_t {
        Idle,
        SendingStart,     // pushing the OTA-start command to peers
        WaitingStartAcks, // waiting for peers to ACK the start command
        WaitingPeers,     // waiting for peers to update + reconnect
        UpdatingMain,     // MAIN performing its own OTA
        Complete,         // done, result available
        Failed,
};
const char *coordinatorPhaseToString(CoordinatorPhase phase);

/// Which step the sequence reached (the host maps these to UI strings —
/// the coordinator carries no i18n dependency).
enum class OtaStep : uint8_t {
        NotifyingPeers, // telling peers to update
        SkippingPeers,  // no peers connected, going straight to MAIN
        WaitingPeers,   // peers acked, waiting for them to finish
        Checking,       // checking the remote version
        Downloading,    // downloading + flashing MAIN
};
enum class OtaResultKind : uint8_t {
        UpToDate,  // already on the latest version
        Installed, // update downloaded + flashed, rebooting
        Failed,    // see detail string
};

/// Total steps in the sequence (for progress display).
constexpr int OTA_TOTAL_STEPS = 4;

/// step in [1..total]; `which` identifies the step.
using OtaStepCallback = void (*)(int step, int total, OtaStep which);
/// `detail` is the error text when kind == Failed, else nullptr.
using OtaResultCallback = void (*)(bool success, OtaResultKind kind, const char *detail);

/// Project-specific peer coordination. The coordinator drives the FSM + the
/// MAIN update; the host knows the node protocol, identities and version.
class CoordinatorHost
{
    public:
        virtual ~CoordinatorHost() = default;

        /// Are any peer nodes connected that should update first? (If none, the
        /// sequence skips straight to the MAIN update.)
        virtual bool hasConnectedPeers() = 0;

        /// Fan out the OTA-start command to the connected peers. Implementations
        /// should also reset their ack tracking here.
        virtual void sendOtaStartToPeers() = 0;

        /// Have all connected peers acknowledged the start command?
        virtual bool allPeersAcked() = 0;

        /// Have all connected peers finished updating and reconnected?
        virtual bool allPeersUpdated() = 0;

        /// The current MAIN firmware version string (for the update check).
        virtual const char *currentFirmwareVersion() = 0;
};

class OtaCoordinator
{
    public:
        OtaCoordinator(OtaUpdater &updater, CoordinatorHost &host);

        /// Optional UI callbacks for progress + result.
        void setCallbacks(OtaStepCallback step_cb, OtaResultCallback result_cb)
        {
                step_cb_ = step_cb;
                result_cb_ = result_cb;
        }

        /// Start the sequence. The caller must ensure connectivity first.
        /// @return false if a sequence is already active.
        bool start();

        /// Tick every loop iteration while a sequence is active.
        void loop(ungula::core::time::tick_ms_t now_ms);

        CoordinatorPhase phase() const
        {
                return phase_;
        }
        bool isActive() const
        {
                return phase_ != CoordinatorPhase::Idle && phase_ != CoordinatorPhase::Complete &&
                       phase_ != CoordinatorPhase::Failed;
        }
        bool updateApplied() const
        {
                return update_applied_;
        }
        int downloadPercent() const
        {
                return download_percent_;
        }
        const char *statusMessage() const
        {
                return status_msg_;
        }

    private:
        OtaUpdater &updater_;
        CoordinatorHost &host_;
        CoordinatorPhase phase_ = CoordinatorPhase::Idle;
        uint32_t phase_start_ms_ = 0;
        bool update_applied_ = false;
        char status_msg_[80] = {};

        volatile int download_percent_ = 0;
        void *ota_task_handle_ = nullptr; // FreeRTOS TaskHandle_t (opaque here)
        volatile bool ota_task_done_ = false;
        volatile bool ota_task_success_ = false;
        char ota_task_error_[80] = {};

        OtaStepCallback step_cb_ = nullptr;
        OtaResultCallback result_cb_ = nullptr;

        static constexpr uint32_t ACK_TIMEOUT_MS = 5000;
        static constexpr uint32_t PEER_UPDATE_TIMEOUT_MS = 30000;
        static constexpr uint32_t PEER_SETTLE_MS = 5000;
        static constexpr uint32_t OTA_TASK_STACK_SIZE = 16384;

        void advancePhase(CoordinatorPhase next);
        void setStatus(const char *msg);
        void fail(const char *reason);
        void startUpdateTask();
        static void otaTaskFunc(void *param);

        void notifyStep(int step, OtaStep which)
        {
                if (step_cb_) {
                        step_cb_(step, OTA_TOTAL_STEPS, which);
                }
        }
        void notifyResult(bool ok, OtaResultKind kind, const char *detail)
        {
                if (result_cb_) {
                        result_cb_(ok, kind, detail);
                }
        }
};

} // namespace ungula::ota

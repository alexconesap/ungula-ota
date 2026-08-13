# UngulaOta

> **High-performance embedded C++ libraries for ESP32, STM32 and other MCUs** — streaming OTA firmware update (HTTP or SD source). Supported targets: ESP32 only.

> **LLM usage note:** if this library is consumed from a coding AI workflow, explicitly point the agent to `API.md` first. `API.md` is the LLM-facing contract (public API + examples + constraints) and avoids wasting time/tokens scanning source files and this human-oriented README.

> **Warning - Active Development:** This library is under active architecture work to support multiple projects in parallel. Its structure is not finalized yet and may change without notice while this work is in progress. Updates are currently frequent (often daily). Target for structural freeze and stable `v1.0.0`: **June 2026**.

OTA firmware update library for ESP32. It streams firmware in chunks from an HTTP server or SD card, and writes it to flash. No full binary buffered in RAM.

The update source (where the firmware lives) and the platform writer (how it gets written) are injected separately, so you can swap between HTTP and SD without touching the core logic.

Concrete adapters use ESP-IDF APIs directly: `esp_http_client` for HTTP, POSIX file I/O via VFS for SD, and `esp_ota_ops` for flash writing.

## Table of Contents

- [C++ Compatibility](#c-compatibility)
- [Directory Structure](#directory-structure)
- [Stack Size Requirement](#stack-size-requirement)
- [Quick Start — HTTP Update](#quick-start--http-update)
- [Quick Start — SD Card](#quick-start--sd-card)
- [Examples](#examples)
  - [Check First, Update Later](#check-first-update-later)
  - [Progress Callback](#progress-callback)
  - [Writing Your Own Source](#writing-your-own-source)
  - [Writing Your Own Writer](#writing-your-own-writer)
- [OtaStatus Codes](#otastatus-codes)
- [Compilation Guards](#compilation-guards)
- [Testing](#testing)
  - [Local development (sibling repos available)](#local-development-sibling-repos-available)
  - [Standalone (no sibling repos)](#standalone-no-sibling-repos)
- [Dependencies](#dependencies)
- [Acknowledgements](#acknowledgements)
- [License](#license)
- [Arduino CLI symlink note (rarely relevant)](#arduino-cli-symlink-note-rarely-relevant)

## C++ Compatibility

- **Own source minimum**: `C++17`.
- **Effective minimum for consumers**: `C++20`.
- **Dependency impact**: Depends on `UngulaNet` (`C++20`), so consumers must compile as `C++20`.

## Directory Structure

```text
lib_ota/src/
  ungula_ota.h                Flat forwarder for Arduino CLI discovery
  ungula/
    ota.h                     Umbrella header (include this one)
    ota/
      core/                       Platform-independent interfaces and logic
        ota_types.h               OtaStatus, OtaProgressCallbackData, OtaProgressCallback, otaStatusToString()
        i_ota_source.h            IOtaSource interface + OtaDataCallback
        i_firmware_writer.h       IFirmwareWriter interface
        ota_version.h             Semantic version comparison (inline)
        ota_updater.h             OtaUpdater facade
        ota_updater.cpp           OtaUpdater implementation
      sources/                    Firmware source adapters
        http/
          esp_idf_http_ota_source.h/.cpp   ESP-IDF esp_http_client (legacy path)
          esp/
            http_ota_source.h/.cpp         ESP-IDF esp_http_client (active, primary)
        sd/
          esp_idf_sd_ota_source.h/.cpp     ESP-IDF POSIX file I/O via VFS
      writers/                    Flash writer adapters
        esp32/
          idf/
            esp32_idf_firmware_writer.h/.cpp   ESP-IDF esp_ota_ops.h
      coordinator/
        ota_coordinator.h/.cpp    Multi-node update FSM (ESP32 only)
```

## Stack Size Requirement

The HTTP OTA source needs significant stack space for TLS (the ESP-IDF HTTP client + mbedTLS allocate heavily on the stack during the TLS handshake).

**Arduino `.ino` projects only**: The Arduino framework runs `loop()` on a FreeRTOS task with a default stack of 8 KB, which is not enough. Add this macro at the top of your `.ino` file, **before** `setup()` or any other code:

```cpp
#include <ungula/ota.h>

SET_LOOP_TASK_STACK_SIZE(16384);
```

This is an Arduino-ESP32 macro that increases the loop task stack to 16 KB. Without it, the OTA download will crash with a stack overflow (`InstrFetchProhibited` or `Stack canary watchpoint triggered`). Applies to all ESP32 variants (ESP32, ESP32-S3, ESP32-C3).

**Pure ESP-IDF or custom FreeRTOS tasks**: This macro does not apply. Instead, allocate at least 16 KB when creating the task that calls `performUpdate()` (e.g. `xTaskCreate(..., 16384, ...)`). If you already run OTA from a task with sufficient stack, no action is needed.

## Quick Start — HTTP Update

```cpp
#include <ungula/ota/core/ota_updater.h>
#include <ungula/ota/sources/http/esp/http_ota_source.h>
#include <ungula/ota/writers/esp32/idf/esp32_idf_firmware_writer.h>
#include <emblogx/logger.h>

using namespace ungula::ota;

static EspHttpOtaSource source("https://updates.example.com/firmware/mydevice", "firmware.bin");
static Esp32IdfFirmwareWriter writer;
static OtaUpdater updater;

void app_main() {
    updater.setSource(&source);
    updater.setWriter(&writer);

    OtaStatus result = updater.performUpdate("1.0.3");
    // If a newer version exists, it downloads, flashes, and reboots.
    // If we reach here, either no update was needed or something failed.
    if (result != OtaStatus::Ok && result != OtaStatus::NoUpdate) {
        log_error("OTA failed: %s", otaStatusToString(result));
    }
}
```

The library fetches `{baseUrl}/version.txt` to get the remote version, compares it with your current version (semantic x.y.z), and if the remote is newer it downloads `{baseUrl}/{binFilename}` in 4KB chunks.

The `EspHttpOtaSource` in `sources/http/esp/` is the primary HTTP source. It uses `esp_http_client` directly and works on both pure ESP-IDF and Arduino-ESP32 projects. The IDF writer uses `esp_ota_begin/write/end` and sets the boot partition on success.

## Quick Start — SD Card

```cpp
#include <ungula/ota/sources/sd/esp_idf_sd_ota_source.h>

// SD must be mounted via VFS beforehand
static EspIdfSdOtaSource source("/sdcard/firmware", "device.bin");
```

The SD source looks for `{basePath}/version.txt` and `{basePath}/{binFilename}`. It uses POSIX `fopen`/`fread` via the VFS layer, so the SD card must be mounted before calling the updater.

## Examples

### Check First, Update Later

If you want to show a prompt on a display before flashing:

```cpp
#include <ungula/ota.h>
#include <emblogx/logger.h>

OtaStatus check = updater.checkForUpdate("1.0.3");
if (check == OtaStatus::Ok) {
    displayMessage("Update available. Starting download...");

    OtaStatus result = updater.performUpdate("1.0.3");
    // Device reboots on success (autoReboot is true by default)
}
```

### Progress Callback

Track download progress. The callback fires for every 4 KB chunk written to flash. Use a threshold (e.g., every 10%) to avoid flooding the log output:

```cpp
#include <ungula/ota.h>
#include <emblogx/logger.h>

// The callback takes ONE argument: OtaProgressCallbackData.
// It is a plain C function pointer, so only non-capturing lambdas
// and free functions convert.
updater.setProgressCallback([](OtaProgressCallbackData data) {
    if (data.totalBytes > 0) {
        int percent = static_cast<int>((data.bytesWritten * 100) / data.totalBytes);
        static int lastPct = -1;
        if ((percent / 10) != (lastPct / 10)) {
            lastPct = percent;
            log_info("OTA: download %d%% (%u / %u bytes)", percent,
                     static_cast<unsigned>(data.bytesWritten),
                     static_cast<unsigned>(data.totalBytes));
        }
    }
});

OtaStatus result = updater.performUpdate("1.0.3");
```

Output:
```text
OTA: download 10% (123997 / 1202496 bytes)
OTA: download 20% (243997 / 1202496 bytes)
...
OTA: download 100% (1202496 / 1202496 bytes)
```

**Important**: Keep the callback lightweight. Do not perform display rendering, memory allocation, or long operations inside the callback — it runs in the HTTP stream context and any delay can cause the download to timeout. For UI updates, store the percentage in a volatile variable and read it from the main loop.

### Writing Your Own Source

Implement `IOtaSource` to pull firmware from somewhere else (BLE, MQTT, a custom protocol):

```cpp
#include <ungula/ota/core/i_ota_source.h>

class BleOtaSource : public ungula::ota::IOtaSource {
public:
    bool fetchVersion(char* out, size_t maxLen) override {
        // Read version string from BLE characteristic
        strncpy(out, remoteVersion_, maxLen);
        return true;
    }

    size_t getFirmwareSize() override {
        return remoteFirmwareSize_;
    }

    bool streamFirmware(OtaDataCallback callback, void* ctx) override {
        // Read chunks from BLE and pass them to the callback.
        // Return false from callback to abort the transfer.
        while (hasMoreData()) {
            uint8_t chunk[512];
            size_t len = readNextChunk(chunk, sizeof(chunk));
            if (!callback(chunk, len, ctx)) return false;
        }
        return true;
    }

private:
    char remoteVersion_[32];
    size_t remoteFirmwareSize_ = 0;
};
```

### Writing Your Own Writer

Implement `IFirmwareWriter` for a different target (external flash, dual-bank, etc.):

```cpp
#include <ungula/ota/core/i_firmware_writer.h>

class ExternalFlashWriter : public ungula::ota::IFirmwareWriter {
public:
    bool begin(size_t totalSize) override {
        // Erase flash sectors, validate space
        return eraseRegion(0, totalSize);
    }

    size_t writeChunk(const uint8_t* data, size_t len) override {
        // Write to external flash at current offset
        if (!flashWrite(offset_, data, len)) return 0;
        offset_ += len;
        return len;
    }

    bool end() override {
        // Verify CRC, mark partition as valid
        return verifyCrc();
    }

    void abort() override {
        offset_ = 0;
    }

private:
    size_t offset_ = 0;
};
```

## Multi-Node Coordinator

`OtaUpdater` updates **one** device. For a system where a controller must update
itself **and** a set of peer nodes, `ungula/ota/coordinator/ota_coordinator.h`
provides `OtaCoordinator` — the update FSM:

1. tell the peers to update first,
2. wait for them to update + reconnect,
3. update MAIN (via `OtaUpdater`),
4. reboot, or report "up to date".

The coordinator carries **no node-protocol and no UI/i18n dependency**. How to
reach the peers (their transport, message format, identities) and the firmware
version are abstracted behind `CoordinatorHost`, which the project implements.
Step/result progress is reported as **enums**; the host maps them to display
text. (ESP32 only — the MAIN download runs on a FreeRTOS task.)

```cpp
#include <ungula/ota/coordinator/ota_coordinator.h>

// 1. Implement the project-specific peer coordination.
class MyOtaHost : public ungula::ota::CoordinatorHost {
public:
    bool hasConnectedPeers() override          { return myNodes.anyConnected(); }
    void sendOtaStartToPeers() override         { acks_.reset(); myNodes.broadcastOtaStart(); }
    bool allPeersAcked() override               { return acks_.allConnectedAcked(); }
    bool allPeersUpdated() override             { return myNodes.allBackOnline(); }
    const char* currentFirmwareVersion() override { return FW_VERSION; }
    void onAck(NodeId n) { acks_.mark(n); }     // call from your transport rx
private:
    AckTracker acks_;
};

// 2. Wire it once (updater is your configured OtaUpdater).
MyOtaHost host;
ungula::ota::OtaCoordinator coord(updater, host);
coord.setCallbacks(
    [](int step, int total, ungula::ota::OtaStep which) { ui_show_step(step, total, which); },
    [](bool ok, ungula::ota::OtaResultKind kind, const char* detail) { ui_show_result(ok, kind, detail); });

// 3. Drive it.
coord.start();                       // caller ensures connectivity first
// every loop while active:
coord.loop(now_ms());
if (coord.phase() == ungula::ota::CoordinatorPhase::UpdatingMain)
    ui_progress(coord.downloadPercent());
```

`OtaStep` (`NotifyingPeers`, `SkippingPeers`, `WaitingPeers`, `Checking`,
`Downloading`) and `OtaResultKind` (`UpToDate`, `Installed`, `Failed`) are the
enums passed to the callbacks. `CoordinatorPhase` is the FSM state. See `API.md`
for the full surface.

### Headless operation (no UI)

The coordinator runs **identically with or without a display** — it depends on
nothing in the UI. To support a headless build:

- **Skip `setCallbacks()`.** The step/result callbacks are optional and exist
  only to feed a UI; a headless build leaves them unset and the FSM runs the
  same. The coordinator reboots MAIN itself on a successful update.
- **Tick `loop()` from the main application loop, not from a UI loop.** If the
  only `coord.loop()` call lives inside the screen redraw path, OTA silently
  stops working when the UI is compiled out. Drive it from the same place that
  ticks the rest of the system.
- **Expose progress by polling, not callbacks.** `isActive()`, `phase()`,
  `downloadPercent()` and `updateApplied()` are enough to surface OTA status
  over a REST endpoint (e.g. `GET /api/system/ota/status`) and to start it
  (`coord.start()` from `POST /api/system/ota/update`) with no display present.

```cpp
// In the main loop — runs whether or not a UI is compiled:
if (coord.isActive()) coord.loop(now_ms);

// REST status handler (headless-friendly):
//   { "active": isActive(), "phase": coordinatorPhaseToString(phase()),
//     "percent": downloadPercent(), "applied": updateApplied() }
```

## OtaStatus Codes

| Status | Meaning |
| --- | --- |
| `Ok` | Update found and applied |
| `NoUpdate` | Already up to date |
| `NoSource` | No source set |
| `NoWriter` | No writer set |
| `FetchVersionFailed` | Could not read version.txt |
| `VersionParseFailed` | version.txt was empty or unreadable |
| `BeginFailed` | Flash writer could not start |
| `StreamFailed` | Download broke mid-transfer |
| `WriteFailed` | Flash writer rejected a chunk |
| `FinalizeFailed` | end() returned false (corrupt image, or setting the boot partition failed) |

Use `otaStatusToString(status)` for logging.

## What the update does and does not guarantee

Guaranteed by the ESP32 writer:

- The boot partition is switched **only after** `esp_ota_end()` accepts the
  image. A power cut, a broken download or a rejected image leaves the running
  firmware untouched — the half-written OTA slot is never booted.
- `esp_ota_begin()` refuses an image larger than the target partition, so an
  oversized binary fails at `BeginFailed` instead of overflowing.

**Not** provided — handle it in the host project if you need it:

- **No signature or checksum check of your own.** The only validation is the
  ESP-IDF image check inside `esp_ota_end()`. Any server that can answer
  `{baseUrl}/{binFilename}` can flash the device. Use HTTPS
  (`EspHttpOtaSource`, which attaches the IDF CA bundle) and enable ESP-IDF
  Secure Boot / flash encryption if the update path is not trusted.
- **No rollback confirmation.** If you build with
  `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, the new image boots in
  `PENDING_VERIFY` and the application must call
  `esp_ota_mark_app_valid_cancel_rollback()` once it considers itself healthy.
  The library never calls it — do it from your own startup code, or the device
  reverts on the next reboot.
- **No resume.** A failed download restarts from byte 0.
- **No version downgrade path.** `compareVersions()` only accepts a strictly
  newer `x.y.z`; a malformed `version.txt` parses as `0.0.0` and reports
  `NoUpdate` rather than an error.

## Compilation Guards

The core (`OtaUpdater`, interfaces, version comparison) compiles unconditionally on any platform.

Each adapter is gated by preprocessor directives:

**Platform flags** — defined automatically by the toolchain:

- `ESP_PLATFORM` — defined by ESP-IDF (also present in Arduino-ESP32)

**Feature flags** — you must define these explicitly in your build configuration (e.g. `build_flags = -DENABLE_OTA_HTTP` in platformio.ini):

- `ENABLE_OTA_HTTP` — enables the HTTP source adapters
- `ENABLE_OTA_SD` — enables the SD card source adapter

The resulting adapter selection:

| Class | Required guards |
| --- | --- |
| `EspHttpOtaSource` | `ENABLE_OTA_HTTP` + `ESP_PLATFORM` |
| `EspIdfSdOtaSource` | `ENABLE_OTA_SD` + `ESP_PLATFORM` |
| `Esp32IdfFirmwareWriter` | `ESP_PLATFORM` |

If `ESP_PLATFORM` is not defined (e.g. desktop unit tests), only the core compiles. If a feature flag is missing, the corresponding source adapter is excluded even if the platform flag is present.

Type aliases (`IdfHttpOtaSource`, `IdfSdOtaSource`, `Esp32FirmwareWriter`) are provided in each header for convenience.

## Testing

### Local development (sibling repos available)

If you have the full workspace with `lib/`, `lib_emblogx/`, and `lib_ota/`
as siblings, just build and run:

```shell
cd lib_ota/tests
chmod +x *.sh
./1_build.sh
./2_run.sh
```

This is the default — CMake uses the sibling directories directly.

### Standalone (no sibling repos)

If you cloned only `lib_ota`, pass `-DUSE_LOCAL_DEPS=OFF` to fetch
dependencies from GitHub automatically:

```shell
cd lib_ota/tests
mkdir build && cd build
cmake .. -DUSE_LOCAL_DEPS=OFF
cmake --build .
ctest --output-on-failure
```

CMake will download [ungula-core](https://github.com/alexconesap/ungula-core.git)
and [emblogx](https://github.com/alexconesap/emblogx.git) into the `vendor/`
folder.

## Dependencies

| Library | Repo | Used for |
| ------- | ---- | -------- |
| UngulaCore | [ungula-core](https://github.com/alexconesap/ungula-core.git) | `SystemControl::reboot()` (headers only, stub in tests) |
| embLogX | [emblogx](https://github.com/alexconesap/emblogx.git) | Logging via `log_error()` |
| UngulaNet | [ungula-net](https://github.com/alexconesap/ungula-net.git) | HTTP client backing `HttpOtaSource` (only when `ENABLE_OTA_HTTP` is set) |

ESP-IDF component dependencies (part of the IDF SDK, no extra components needed):

- **esp_http_client** — when `ENABLE_OTA_HTTP` is set
- **esp_ota_ops** — firmware writer
- **VFS + sdmmc** — when `ENABLE_OTA_SD` is set (SD must be mounted before use)

For local development, keep the libraries as siblings:

```text
your_workspace/
  lib/            <- UngulaCore
  lib_emblogx/    <- embLogX
  lib_ota/        <- this library
```

## Acknowledgements

Thanks to Claude and ChatGPT for helping on generating this documentation.

## License

MIT License — see [LICENSE](LICENSE) file.

---

## Arduino CLI symlink note (rarely relevant)

This library ships a flat forwarder header at `src/ungula_ota.h` that
just `#include`s `ungula/ota.h`. `library.properties` `includes=` points
at the forwarder.

It only exists to work around an Arduino CLI quirk: when the library is
consumed through a symlink, the CLI sometimes fails to discover headers
nested under `src/ungula/`. The flat forwarder fixes that scan.

**Host code keeps including the real header**:

```cpp
#include <ungula/ota.h>
```

PlatformIO, ESP-IDF component builds, and plain CMake setups can ignore
the forwarder.

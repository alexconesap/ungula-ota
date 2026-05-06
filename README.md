# UngulaOta

> **High-performance embedded C++ libraries for ESP32, STM32 and other MCUs** — streaming OTA firmware update (HTTP or SD source). Supported targets: ESP32 only.

OTA firmware update library for ESP32. It streams firmware in chunks from an HTTP server or SD card, and writes it to flash. No full binary buffered in RAM.

The update source (where the firmware lives) and the platform writer (how it gets written) are injected separately, so you can swap between HTTP and SD without touching the core logic.

Concrete adapters use ESP-IDF APIs directly: `esp_http_client` for HTTP, POSIX file I/O via VFS for SD, and `esp_ota_ops` for flash writing.

## Directory Structure

```text
lib_ota/src/
  ungula_ota.h              Umbrella header (triggers path discovery for Arduino .ino projects)
  ota/
    core/                       Platform-independent interfaces and logic
      ota_types.h               OtaStatus enum, OtaProgressCallback, otaStatusToString()
      i_ota_source.h            IOtaSource interface (function pointer callback)
      i_firmware_writer.h       IFirmwareWriter interface
      ota_version.h             Semantic version comparison (inline)
      ota_updater.h             OtaUpdater facade
      ota_updater.cpp           OtaUpdater implementation
    sources/                    Firmware source adapters
      http/
        esp_idf_http_ota_source.h/.cpp     ESP-IDF esp_http_client (legacy path)
        esp/
          http_ota_source.h/.cpp           ESP-IDF esp_http_client (active, primary)
      sd/
        esp_idf_sd_ota_source.h/.cpp       ESP-IDF POSIX file I/O via VFS
    writers/                    Flash writer adapters
      esp32/
        idf/
          esp32_idf_firmware_writer.h/.cpp       ESP-IDF esp_ota_ops.h
```

## Stack Size Requirement

The HTTP OTA source needs significant stack space for TLS (the ESP-IDF HTTP client + mbedTLS allocate heavily on the stack during the TLS handshake).

**Arduino `.ino` projects only**: The Arduino framework runs `loop()` on a FreeRTOS task with a default stack of 8 KB, which is not enough. Add this macro at the top of your `.ino` file, **before** `setup()` or any other code:

```cpp
SET_LOOP_TASK_STACK_SIZE(16384);
```

This is an Arduino-ESP32 macro that increases the loop task stack to 16 KB. Without it, the OTA download will crash with a stack overflow (`InstrFetchProhibited` or `Stack canary watchpoint triggered`). Applies to all ESP32 variants (ESP32, ESP32-S3, ESP32-C3).

**Pure ESP-IDF or custom FreeRTOS tasks**: This macro does not apply. Instead, allocate at least 16 KB when creating the task that calls `performUpdate()` (e.g. `xTaskCreate(..., 16384, ...)`). If you already run OTA from a task with sufficient stack, no action is needed.

## Quick Start — HTTP Update

```cpp
#include <ungula/ota/core/ota_updater.h>
#include <ungula/ota/sources/http/esp/http_ota_source.h>
#include <ungula/ota/writers/esp32/idf/esp32_idf_firmware_writer.h>

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
updater.setProgressCallback([](size_t bytesWritten, size_t totalBytes) {
    if (totalBytes > 0) {
        int percent = static_cast<int>((bytesWritten * 100) / totalBytes);
        static int lastPct = -1;
        if ((percent / 10) != (lastPct / 10)) {
            lastPct = percent;
            log_info("OTA: download %d%% (%u / %u bytes)", percent,
                     static_cast<unsigned>(bytesWritten),
                     static_cast<unsigned>(totalBytes));
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
| `FinalizeFailed` | end() returned false (corrupt image) |

Use `otaStatusToString(status)` for logging.

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

Type aliases (`HttpOtaSource`, `SdOtaSource`, `Esp32FirmwareWriter`) are provided in each header for convenience.

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

MIT License — see [LICENSE](license.txt) file.

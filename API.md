# UngulaOta

Streaming OTA firmware update for ESP32. Pulls a remote `version.txt`,
compares against the running version, then streams the firmware binary
in chunks from a transport (HTTP/HTTPS or SD) into a flash writer
(`esp_ota_ops`). Source and writer are decoupled behind interfaces, so
new transports or targets plug in without changing the core flow.

---

## LLM quick map

- **Primary include**: `#include <ungula/ota.h>`.
- **Arduino discovery include**: `#include <ungula_ota.h>` (forwarder only; host code should keep using the real header).
- **Namespace root**: `ungula::ota`.
- **Language baseline**: C++17 minimum (examples avoid post-C++17 requirements).
- **Supported architectures**: `esp32`.
- **Read order for coding agents**: `Usage` (working patterns) -> `API` (symbols/signatures) -> `Lifecycle`/`Error handling`/`Threading` notes in this file.

### Use-case index

- [Use case: HTTP/HTTPS update with auto-reboot](#use-case-httphttps-update-with-auto-reboot)
- [Use case: Check first, install later](#use-case-check-first-install-later)
- [Use case: Progress reporting](#use-case-progress-reporting)
- [Use case: SD card update](#use-case-sd-card-update)
- [Use case: Custom transport (implement IOtaSource)](#use-case-custom-transport-implement-iotasource)
- [Use case: Custom flash target (implement IFirmwareWriter)](#use-case-custom-flash-target-implement-ifirmwarewriter)

### LLM rules

- Use only symbols and include paths documented in this file; do not infer extra public API from implementation files.
- Prefer the use-case patterns here over ad-hoc rewrites; keep dependency wiring and lifecycle order identical unless the task explicitly changes API design.
- Treat headers under `detail/`, `platform/`, and `platforms/` as internal unless this document calls them out as public.
- If required behavior is missing from the documented API, report the gap explicitly instead of inventing new public symbols.


## Usage

All examples assume `using namespace ungula::ota;`.

### Use case: HTTP/HTTPS update with auto-reboot

```cpp
#include <ungula/ota.h>
#include <ungula/ota/sources/http/esp/http_ota_source.h>
#include <ungula/ota/writers/esp32/idf/esp32_idf_firmware_writer.h>

using namespace ungula::ota;

static EspHttpOtaSource source(
    "https://updates.example.com/firmware/icb",  // baseUrl
    "ICB.ino.bin");                              // binFilename
static Esp32IdfFirmwareWriter writer;
static OtaUpdater updater;

extern "C" void app_main() {
    updater.setSource(&source);
    updater.setWriter(&writer);

    OtaStatus result = updater.performUpdate("1.0.3");
    // On Ok with autoReboot=true (default), device reboots and never returns.
    if (result != OtaStatus::Ok && result != OtaStatus::NoUpdate) {
        // log via your project's logger
    }
}
```

When to use this: standard pull-based OTA against an HTTPS server that
hosts `{baseUrl}/version.txt` and `{baseUrl}/{binFilename}`. Requires
`-DENABLE_OTA_HTTP` and `-DESP_PLATFORM` (the latter is set by ESP-IDF).

### Use case: Check first, install later

```cpp
#include <ungula/ota.h>

using namespace ungula::ota;

void maybeUpdate(OtaUpdater& updater, const char* currentVersion) {
    OtaStatus check = updater.checkForUpdate(currentVersion);
    if (check != OtaStatus::Ok) {
        return;  // NoUpdate or an error
    }

    const char* remote = updater.getRemoteVersion();
    // show prompt / wait for user / write to display using `remote`...

    OtaStatus install = updater.downloadAndInstall(/*autoReboot=*/true);
    (void)install;  // device reboots on Ok
}
```

When to use this: UI prompt before flashing, or batching the install
into a maintenance window after detecting an update.

### Use case: Progress reporting

```cpp
#include <ungula/ota.h>

using namespace ungula::ota;

static volatile int g_otaPercent = 0;

static void onProgress(OtaProgressCallbackData data) {
    if (data.totalBytes > 0) {
        g_otaPercent = static_cast<int>((data.bytesWritten * 100) / data.totalBytes);
    }
}

void wireProgress(OtaUpdater& updater) {
    updater.setProgressCallback(&onProgress);
}
```

When to use this: surface download progress to UI/logs. Callback runs in
the streaming context — keep it short, no allocation, no blocking I/O.
The callback is a plain C function pointer, so non-capturing lambdas and
free functions are fine; capturing lambdas are not.

### Use case: SD card update

```cpp
#include <ungula/ota.h>
#include <ungula/ota/sources/sd/esp_idf_sd_ota_source.h>
#include <ungula/ota/writers/esp32/idf/esp32_idf_firmware_writer.h>

using namespace ungula::ota;

// SD card must already be mounted via sdmmc_host + esp_vfs_fat
// at the path used as basePath.
static EspIdfSdOtaSource source("/sdcard/firmware/icb", "ICB.ino.bin");
static Esp32IdfFirmwareWriter writer;
static OtaUpdater updater;

void runSdUpdate() {
    updater.setSource(&source);
    updater.setWriter(&writer);
    updater.performUpdate("1.0.3");
}
```

When to use this: offline / field updates from removable media. Requires
`-DENABLE_OTA_SD` and `-DESP_PLATFORM`.

### Use case: Custom transport (implement IOtaSource)

```cpp
#include <ungula/ota.h>
#include <cstring>

class MyOtaSource : public ungula::ota::IOtaSource {
    public:
        bool fetchVersion(char* out, size_t maxLen) override {
            std::strncpy(out, "1.2.0", maxLen);
            return true;
        }

        size_t getFirmwareSize() override {
            return firmwareSize_;
        }

        bool streamFirmware(ungula::ota::OtaDataCallback cb, void* ctx) override {
            // MUST set firmwareSize_ before calling cb the first time.
            firmwareSize_ = totalKnownSize();
            uint8_t buf[1024];
            while (size_t n = readNext(buf, sizeof(buf))) {
                if (!cb(buf, n, ctx)) return false;  // caller aborted
            }
            return true;
        }

    private:
        size_t firmwareSize_ = 0;
        size_t totalKnownSize();
        size_t readNext(uint8_t* dst, size_t cap);
};
```

When to use this: BLE, MQTT, custom binary protocol, or any non-HTTP/SD
firmware origin. The core never assumes a specific transport.

### Use case: Custom flash target (implement IFirmwareWriter)

```cpp
#include <ungula/ota.h>

class MyFlashWriter : public ungula::ota::IFirmwareWriter {
    public:
        bool begin(size_t totalSize) override {
            // totalSize == 0 means "unknown" — use platform sentinel
            return prepareRegion(totalSize);
        }
        size_t writeChunk(const uint8_t* data, size_t len) override {
            return programFlash(offset_, data, len) ? (offset_ += len, len) : 0;
        }
        bool end() override { return verifyAndCommit(); }
        void abort() override { offset_ = 0; rollback(); }

    private:
        size_t offset_ = 0;
        bool prepareRegion(size_t);
        bool programFlash(size_t, const uint8_t*, size_t);
        bool verifyAndCommit();
        void rollback();
};
```

When to use this: external SPI flash, dual-bank schemes, non-ESP32
targets.

---

## API

Header layout under `lib_ota/src/`:

| Header | Public symbols |
| --- | --- |
| `ungula/ota.h` | Umbrella; pulls in everything below |
| `ungula/ota/core/ota_types.h` | `OtaStatus`, `OtaProgressCallback`, `OtaProgressCallbackData`, `otaStatusToString()` |
| `ungula/ota/core/i_ota_source.h` | `IOtaSource`, `OtaDataCallback` |
| `ungula/ota/core/i_firmware_writer.h` | `IFirmwareWriter` |
| `ungula/ota/core/ota_version.h` | `compareVersions()` |
| `ungula/ota/core/ota_updater.h` | `OtaUpdater` (the facade) |
| `ungula/ota/sources/http/esp/http_ota_source.h` | `EspHttpOtaSource`, alias `HttpOtaSource` |
| `ungula/ota/sources/http/esp_idf_http_ota_source.h` | `EspIdfHttpOtaSource`, alias `IdfHttpOtaSource` (legacy) |
| `ungula/ota/sources/sd/esp_idf_sd_ota_source.h` | `EspIdfSdOtaSource`, alias `IdfSdOtaSource` |
| `ungula/ota/writers/esp32/idf/esp32_idf_firmware_writer.h` | `Esp32IdfFirmwareWriter`, alias `Esp32FirmwareWriter` |

All public symbols live in `ungula::ota`.

---

## Public types

### `enum class OtaStatus : uint8_t`

| Variant | Meaning |
| --- | --- |
| `Ok` | Update available (after `checkForUpdate`) or applied (after `downloadAndInstall` / `performUpdate`) |
| `NoUpdate` | Remote version equal to or older than current |
| `NoSource` | `setSource()` was never called |
| `NoWriter` | `setWriter()` was never called |
| `FetchVersionFailed` | `IOtaSource::fetchVersion()` returned false |
| `VersionParseFailed` | Remote version string was empty |
| `BeginFailed` | `IFirmwareWriter::begin()` returned false (no space, partition issue) |
| `StreamFailed` | `IOtaSource::streamFirmware()` aborted or delivered no data |
| `WriteFailed` | `IFirmwareWriter::writeChunk()` wrote fewer bytes than supplied |
| `FinalizeFailed` | `IFirmwareWriter::end()` returned false (image invalid) |

### `struct OtaProgressCallbackData`

```cpp
struct OtaProgressCallbackData {
    size_t bytesWritten;   // cumulative bytes successfully written
    size_t totalBytes;     // total firmware size, or 0 if unknown
};
```

### `using OtaProgressCallback = void (*)(OtaProgressCallbackData)`

Plain C function pointer. No captures.

### `using OtaDataCallback = bool (*)(const uint8_t* data, size_t len, void* ctx)`

Returned by `IOtaSource::streamFirmware`. Return `false` to abort.

### `class IOtaSource` (abstract)

| Method | Contract |
| --- | --- |
| `bool fetchVersion(char* out, size_t maxLen)` | Write null-terminated version string into `out`. Return `true` on success. |
| `size_t getFirmwareSize()` | Bytes in firmware image, or `0` if not yet known. HTTP sources populate after the response headers; SD sources populate during `fetchVersion()`. |
| `bool streamFirmware(OtaDataCallback cb, void* ctx)` | Deliver bytes via `cb`. **Must** make `getFirmwareSize()` valid before the first `cb()` call. Return `true` if all data was delivered. |

### `class IFirmwareWriter` (abstract)

| Method | Contract |
| --- | --- |
| `bool begin(size_t totalSize)` | Open a write session. `totalSize == 0` means unknown — implementation must map to the platform's unknown-size constant. |
| `size_t writeChunk(const uint8_t*, size_t len)` | Write a chunk; return bytes actually written. Anything `< len` triggers `WriteFailed`. |
| `bool end()` | Finalize / commit. Return `true` if the image is valid. |
| `void abort()` | Discard the in-progress write. Called by the updater on any mid-stream failure. |

### `class OtaUpdater`

Default-constructible facade. Owns nothing — caller keeps source and
writer alive.

| Method | Returns | Notes |
| --- | --- | --- |
| `void setSource(IOtaSource*)` | — | Inject source. |
| `void setWriter(IFirmwareWriter*)` | — | Inject writer. |
| `void setProgressCallback(OtaProgressCallback)` | — | Optional. |
| `OtaStatus checkForUpdate(const char* currentVersion)` | `Ok` if remote is newer, `NoUpdate` otherwise, error code on failure | Caches remote version string. |
| `OtaStatus performUpdate(const char* currentVersion, bool autoReboot=true)` | `Ok` only when `autoReboot=false`; otherwise the device reboots before returning. `NoUpdate` if no update needed. | Equivalent to `checkForUpdate` then `downloadAndInstall`. |
| `OtaStatus downloadAndInstall(bool autoReboot=true)` | Same as above but skips the version check. | Call after `checkForUpdate` returned `Ok`. |
| `const char* getRemoteVersion() const` | Pointer to internal 32-byte buffer with the last fetched version. | Valid after `checkForUpdate()` returns `Ok` or `NoUpdate`. |

### `const char* otaStatusToString(OtaStatus)`

Short lowercase token for logging (`"ok"`, `"no_update"`, `"begin_failed"`, ...).

### `int compareVersions(const char* remote, const char* current)` (inline)

Parses two `x.y.z` strings via `sscanf`. Returns `1` if remote is newer,
`0` if equal, `-1` if older. Missing components default to 0.

### Concrete adapters

| Class | Constructor | Guards |
| --- | --- | --- |
| `EspHttpOtaSource(const char* baseUrl, const char* binFilename)` | Fetches `{baseUrl}/version.txt` and `{baseUrl}/{binFilename}` via `esp_http_client` (HTTP or HTTPS via the IDF CA bundle). | `ENABLE_OTA_HTTP && ESP_PLATFORM` |
| `EspIdfHttpOtaSource(const char* baseUrl, const char* binFilename)` | Legacy ESP-IDF HTTP source. Prefer `EspHttpOtaSource`. | `ENABLE_OTA_HTTP` |
| `EspIdfSdOtaSource(const char* basePath, const char* binFilename)` | Reads `{basePath}/version.txt` and `{basePath}/{binFilename}` via POSIX `fopen`/`fread` over VFS. SD must be mounted first. | `ENABLE_OTA_SD && ESP_PLATFORM` |
| `Esp32IdfFirmwareWriter` | Default-constructible. Wraps `esp_ota_begin`/`esp_ota_write`/`esp_ota_end` and sets the boot partition on success. | `ESP_PLATFORM` |

Backward-compatible aliases also exist: `HttpOtaSource`,
`IdfHttpOtaSource`, `IdfSdOtaSource`, `Esp32FirmwareWriter`.

---

## Lifecycle

```
construct OtaUpdater
  └─ setSource(&src)
  └─ setWriter(&wr)
  └─ setProgressCallback(cb)        // optional
  └─ checkForUpdate(current)        // optional pre-step
  └─ performUpdate(current)         // or downloadAndInstall()
       │
       ├─ source.fetchVersion()
       ├─ compareVersions()
       ├─ source.streamFirmware()
       │     ├─ first chunk: writer.begin(source.getFirmwareSize())
       │     ├─ writer.writeChunk(...) per chunk
       │     └─ progressCb(...) per chunk
       ├─ writer.end()              // commit / set boot partition
       └─ SystemControl::rebootAfterMs(500)   // if autoReboot
```

Required ordering:

1. `setSource` and `setWriter` **before** any `check*` / `*Update*` /
   `downloadAndInstall` call. Missing either yields `NoSource` /
   `NoWriter`.
2. `IOtaSource` implementations **must** populate `getFirmwareSize()`
   before invoking the first data callback. The updater calls
   `writer.begin()` lazily on the first chunk, using that size — passing
   `0` would force the writer into unknown-size mode and break finalize
   on ESP32.
3. On any mid-stream failure (`StreamFailed`, `WriteFailed`,
   `BeginFailed`), the updater calls `writer.abort()` automatically.
   Custom writers must release resources there.
4. On `Ok` with `autoReboot=true`, control does not return — the device
   reboots ~500 ms later via `ungula::core::system::SystemControl::rebootAfterMs`.

Source and writer are not owned by `OtaUpdater`. Keep them alive (static
storage is the typical pattern) for the duration of the update.

---

## Error handling

All public entry points return `OtaStatus`. There are no exceptions and
no out-parameters for errors.

Recovery patterns:

- `NoSource` / `NoWriter`: programmer error. Fix wiring at boot.
- `FetchVersionFailed`, `StreamFailed`: transient. Retry later. Network
  / SD reachability issue.
- `VersionParseFailed`: server delivered an empty `version.txt`. Treat
  as configuration bug.
- `BeginFailed`: usually no free OTA slot or partition table mismatch.
  Not retryable without operator action.
- `WriteFailed`: flash hardware fault or out-of-space mid-write. Writer
  has been aborted — safe to retry the whole flow.
- `FinalizeFailed`: image arrived but failed signature/CRC checks.
  Image is rejected; current firmware keeps running. Retry to re-download.

Use `otaStatusToString(status)` to log the outcome.

---

## Threading / timing / hardware notes

- `performUpdate` / `downloadAndInstall` are **blocking** until the
  whole image is streamed. Run them from a dedicated task.
- The HTTP path needs significant stack for the TLS handshake. On
  Arduino-ESP32, set `SET_LOOP_TASK_STACK_SIZE(16384);` at file scope of
  the `.ino`. On pure ESP-IDF / FreeRTOS, allocate at least 16 KB to the
  task that calls the updater (`xTaskCreate(..., 16384, ...)`).
- The progress callback runs inside `streamFirmware` on the same task.
  Do not block, allocate, or call into UI/display drivers from it; copy
  the percentage to a `volatile` and read it elsewhere.
- `OtaUpdater` itself is not thread-safe. One updater per task.
- ESP32 writer requires a valid OTA partition layout (`ota_0`, `ota_1`,
  `otadata`). Single-app partition tables fail at `begin()`.
- SD source requires the card to already be mounted via VFS at
  `basePath` before `performUpdate` is called.

---

## Internals not part of the public API

| Name | Reason |
| --- | --- |
| `StreamCtx` (in `ota_updater.cpp`) | Internal context wiring source ↔ writer ↔ progress; not exported. |
| `streamCallback` (in `ota_updater.cpp`) | The function-pointer trampoline used by the updater itself. |
| `Esp32IdfFirmwareWriter::handle_`, `partition_` | ESP-IDF handle state — do not touch. |
| Private members of `EspHttpOtaSource` / `EspIdfSdOtaSource` (`baseUrl_`, `binFilename_`, `firmwareSize_`) | Construction-time configuration only. |
| `log_error(...)` calls inside `ota_updater.cpp` | Carry-over EmblogX dependency; treated as tolerated debt per project rules. Do not extend logging usage in new adapters. |
| Direct calls to `esp_http_client`, `esp_ota_ops`, POSIX FS in adapters | Encapsulated by the adapter — call the adapter, not the SDK. |

---

## LLM usage rules

- Use only the documented public API unless explicitly modifying the
  library.
- Prefer the use-case-level API (`OtaUpdater::performUpdate`) over
  driving `IOtaSource` and `IFirmwareWriter` by hand.
- Don't read implementation files to figure out usage — if it's not
  here, it's not public.
- Don't depend on the function-pointer streaming context (`StreamCtx`,
  `streamCallback`) — they are private.
- The progress callback signature is
  `void(OtaProgressCallbackData)`, not `void(size_t, size_t)`. Do not
  use the older two-argument form.
- New adapters must implement the `IOtaSource` / `IFirmwareWriter`
  contracts exactly — including setting `getFirmwareSize()` before the
  first stream callback and supporting `abort()` correctly.
- Do not call Arduino `Update`/`HTTPUpdate` APIs alongside this library.
- Preserve the terminology this API uses: source, writer, stream,
  begin/end, finalize, status.

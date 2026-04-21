#include <gtest/gtest.h>
#include <ota/core/i_firmware_writer.h>
#include <ota/core/i_ota_source.h>
#include <ota/core/ota_updater.h>

#include <cstring>
#include <vector>

using namespace ungula::ota;

// --- Mock OTA source ---

class MockOtaSource : public IOtaSource {
    public:
        std::string version = "1.0.1";
        bool fetchFails = false;
        bool streamFails = false;
        size_t firmwareSize = 1024;
        std::vector<uint8_t> firmwareData;

        MockOtaSource() : firmwareData(1024, 0xAA) {}

        bool fetchVersion(char* out, size_t maxLen) override {
            if (fetchFails)
                return false;
            strncpy(out, version.c_str(), maxLen - 1);
            out[maxLen - 1] = '\0';
            return true;
        }

        size_t getFirmwareSize() override {
            return firmwareSize;
        }

        bool streamFirmware(OtaDataCallback callback, void* ctx) override {
            if (streamFails)
                return false;
            return callback(firmwareData.data(), firmwareData.size(), ctx);
        }
};

// --- Mock firmware writer ---

class MockFirmwareWriter : public IFirmwareWriter {
    public:
        bool beginFails = false;
        bool writeFails = false;
        bool endFails = false;
        size_t totalWritten = 0;
        bool aborted = false;

        bool begin(size_t totalSize) override {
            (void)totalSize;
            return !beginFails;
        }

        size_t writeChunk(const uint8_t* data, size_t len) override {
            (void)data;
            if (writeFails)
                return 0;
            totalWritten += len;
            return len;
        }

        bool end() override {
            return !endFails;
        }
        void abort() override {
            aborted = true;
        }
};

// --- Tests ---

TEST(OtaUpdater, CheckForUpdateNoSource) {
    OtaUpdater updater;
    EXPECT_EQ(updater.checkForUpdate("1.0.0"), OtaStatus::NoSource);
}

TEST(OtaUpdater, CheckForUpdateAvailable) {
    OtaUpdater updater;
    MockOtaSource source;
    source.version = "1.0.2";
    updater.setSource(&source);

    EXPECT_EQ(updater.checkForUpdate("1.0.1"), OtaStatus::Ok);
}

TEST(OtaUpdater, CheckForUpdateNotAvailable) {
    OtaUpdater updater;
    MockOtaSource source;
    source.version = "1.0.0";
    updater.setSource(&source);

    EXPECT_EQ(updater.checkForUpdate("1.0.0"), OtaStatus::NoUpdate);
}

TEST(OtaUpdater, CheckForUpdateOlder) {
    OtaUpdater updater;
    MockOtaSource source;
    source.version = "0.9.0";
    updater.setSource(&source);

    EXPECT_EQ(updater.checkForUpdate("1.0.0"), OtaStatus::NoUpdate);
}

TEST(OtaUpdater, CheckForUpdateFetchFails) {
    OtaUpdater updater;
    MockOtaSource source;
    source.fetchFails = true;
    updater.setSource(&source);

    EXPECT_EQ(updater.checkForUpdate("1.0.0"), OtaStatus::FetchVersionFailed);
}

TEST(OtaUpdater, CheckForUpdateEmptyVersion) {
    OtaUpdater updater;
    MockOtaSource source;
    source.version = "";
    updater.setSource(&source);

    EXPECT_EQ(updater.checkForUpdate("1.0.0"), OtaStatus::VersionParseFailed);
}

TEST(OtaUpdater, PerformUpdateNoWriter) {
    OtaUpdater updater;
    MockOtaSource source;
    source.version = "1.0.2";
    updater.setSource(&source);
    // No writer set

    EXPECT_EQ(updater.performUpdate("1.0.0", false), OtaStatus::NoWriter);
}

TEST(OtaUpdater, PerformUpdateSuccess) {
    OtaUpdater updater;
    MockOtaSource source;
    MockFirmwareWriter writer;
    source.version = "1.0.2";
    updater.setSource(&source);
    updater.setWriter(&writer);

    EXPECT_EQ(updater.performUpdate("1.0.0", false), OtaStatus::Ok);
    EXPECT_EQ(writer.totalWritten, 1024u);
}

TEST(OtaUpdater, PerformUpdateBeginFails) {
    OtaUpdater updater;
    MockOtaSource source;
    MockFirmwareWriter writer;
    source.version = "1.0.2";
    writer.beginFails = true;
    updater.setSource(&source);
    updater.setWriter(&writer);

    EXPECT_EQ(updater.performUpdate("1.0.0", false), OtaStatus::BeginFailed);
}

TEST(OtaUpdater, PerformUpdateStreamFails) {
    OtaUpdater updater;
    MockOtaSource source;
    MockFirmwareWriter writer;
    source.version = "1.0.2";
    source.streamFails = true;
    updater.setSource(&source);
    updater.setWriter(&writer);

    EXPECT_EQ(updater.performUpdate("1.0.0", false), OtaStatus::StreamFailed);
    // abort() is only called if begin() was called first. When the stream
    // fails before delivering any data, begin() was never called (lazy init).
    EXPECT_FALSE(writer.aborted);
}

TEST(OtaUpdater, PerformUpdateWriteFails) {
    OtaUpdater updater;
    MockOtaSource source;
    MockFirmwareWriter writer;
    source.version = "1.0.2";
    writer.writeFails = true;
    updater.setSource(&source);
    updater.setWriter(&writer);

    EXPECT_EQ(updater.performUpdate("1.0.0", false), OtaStatus::WriteFailed);
    EXPECT_TRUE(writer.aborted);
}

TEST(OtaUpdater, PerformUpdateFinalizeFails) {
    OtaUpdater updater;
    MockOtaSource source;
    MockFirmwareWriter writer;
    source.version = "1.0.2";
    writer.endFails = true;
    updater.setSource(&source);
    updater.setWriter(&writer);

    EXPECT_EQ(updater.performUpdate("1.0.0", false), OtaStatus::FinalizeFailed);
}

TEST(OtaUpdater, PerformUpdateNoUpdateAvailable) {
    OtaUpdater updater;
    MockOtaSource source;
    MockFirmwareWriter writer;
    source.version = "1.0.0";
    updater.setSource(&source);
    updater.setWriter(&writer);

    EXPECT_EQ(updater.performUpdate("1.0.0", false), OtaStatus::NoUpdate);
    EXPECT_EQ(writer.totalWritten, 0u);
}

TEST(OtaUpdater, ProgressCallbackInvoked) {
    OtaUpdater updater;
    MockOtaSource source;
    MockFirmwareWriter writer;
    source.version = "1.0.2";
    updater.setSource(&source);
    updater.setWriter(&writer);

    updater.setProgressCallback([](OtaProgressCallbackData data) {
        (void)data;
        // Callback is invoked but we can't capture in a C function pointer
        // This test verifies it doesn't crash
    });

    EXPECT_EQ(updater.performUpdate("1.0.0", false), OtaStatus::Ok);
}

// --- Mock writer that records the totalSize passed to begin() ---

class SizeCapturingWriter : public IFirmwareWriter {
    public:
        size_t capturedSize = 999;
        size_t totalWritten = 0;

        bool begin(size_t totalSize) override {
            capturedSize = totalSize;
            return true;
        }
        size_t writeChunk(const uint8_t* data, size_t len) override {
            (void)data;
            totalWritten += len;
            return len;
        }
        bool end() override {
            return true;
        }
        void abort() override {}
};

TEST(OtaUpdater, BeginReceivesCorrectFirmwareSize) {
    OtaUpdater updater;
    MockOtaSource source;
    SizeCapturingWriter writer;
    source.version = "1.0.2";
    source.firmwareSize = 65536;
    updater.setSource(&source);
    updater.setWriter(&writer);

    EXPECT_EQ(updater.performUpdate("1.0.0", false), OtaStatus::Ok);
    EXPECT_EQ(writer.capturedSize, 65536u);
}

TEST(OtaUpdater, BeginReceivesZeroWhenSizeUnknown) {
    OtaUpdater updater;
    MockOtaSource source;
    SizeCapturingWriter writer;
    source.version = "1.0.2";
    source.firmwareSize = 0;  // source cannot determine size
    updater.setSource(&source);
    updater.setWriter(&writer);

    EXPECT_EQ(updater.performUpdate("1.0.0", false), OtaStatus::Ok);
    EXPECT_EQ(writer.capturedSize, 0u);
    EXPECT_EQ(writer.totalWritten, 1024u);  // data still streamed
}

TEST(OtaUpdater, StatusToString) {
    EXPECT_STREQ(otaStatusToString(OtaStatus::Ok), "ok");
    EXPECT_STREQ(otaStatusToString(OtaStatus::NoUpdate), "no_update");
    EXPECT_STREQ(otaStatusToString(OtaStatus::NoSource), "no_source");
    EXPECT_STREQ(otaStatusToString(OtaStatus::NoWriter), "no_writer");
    EXPECT_STREQ(otaStatusToString(OtaStatus::FetchVersionFailed), "fetch_version_failed");
    EXPECT_STREQ(otaStatusToString(OtaStatus::VersionParseFailed), "version_parse_failed");
    EXPECT_STREQ(otaStatusToString(OtaStatus::BeginFailed), "begin_failed");
    EXPECT_STREQ(otaStatusToString(OtaStatus::StreamFailed), "stream_failed");
    EXPECT_STREQ(otaStatusToString(OtaStatus::WriteFailed), "write_failed");
    EXPECT_STREQ(otaStatusToString(OtaStatus::FinalizeFailed), "finalize_failed");
}

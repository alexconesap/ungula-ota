#include <gtest/gtest.h>
#include <ungula/ota/core/ota_version.h>

using namespace ungula::ota;

TEST(OtaVersion, EqualVersions)
{
    EXPECT_EQ(compareVersions("1.0.0", "1.0.0"), 0);
    EXPECT_EQ(compareVersions("2.3.4", "2.3.4"), 0);
}

TEST(OtaVersion, RemoteNewer)
{
    EXPECT_EQ(compareVersions("1.0.1", "1.0.0"), 1);
    EXPECT_EQ(compareVersions("1.1.0", "1.0.9"), 1);
    EXPECT_EQ(compareVersions("2.0.0", "1.9.9"), 1);
}

TEST(OtaVersion, RemoteOlder)
{
    EXPECT_EQ(compareVersions("1.0.0", "1.0.1"), -1);
    EXPECT_EQ(compareVersions("1.0.9", "1.1.0"), -1);
    EXPECT_EQ(compareVersions("1.9.9", "2.0.0"), -1);
}

TEST(OtaVersion, MajorTakesPrecedence)
{
    EXPECT_EQ(compareVersions("2.0.0", "1.9.9"), 1);
    EXPECT_EQ(compareVersions("1.9.9", "2.0.0"), -1);
}

TEST(OtaVersion, MinorTakesPrecedenceOverPatch)
{
    EXPECT_EQ(compareVersions("1.2.0", "1.1.9"), 1);
}

TEST(OtaVersion, LargeVersionNumbers)
{
    EXPECT_EQ(compareVersions("10.20.300", "10.20.299"), 1);
    EXPECT_EQ(compareVersions("10.20.300", "10.20.300"), 0);
}

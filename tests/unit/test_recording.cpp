/**
 *  @file       test_recording.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Unit tests for recording serialization.
 */

#include "threveal/analysis/event_store.hpp"
#include "threveal/core/events.hpp"
#include "threveal/core/topology.hpp"
#include "threveal/core/types.hpp"
#include "threveal/storage/recording.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <vector>

using threveal::core::CpuId;
using threveal::core::MigrationEvent;
using threveal::core::PmuSample;
using threveal::core::TopologyMap;
using threveal::storage::loadRecording;
using threveal::storage::parseRecording;
using threveal::storage::Recording;
using threveal::storage::RecordingError;
using threveal::storage::saveRecording;
using threveal::storage::serializeRecording;

namespace
{

auto makeRecording() -> Recording
{
    std::vector<CpuId> p_cores = {0, 1, 2};
    std::vector<CpuId> e_cores = {12, 13};

    Recording recording{
        .topology = TopologyMap{p_cores, e_cores},
        .duration_ns = 5'000'000'000,
        .events = {},
    };

    MigrationEvent migration{
        .timestamp_ns = 2'000,
        .pid = 7,
        .tid = 42,
        .src_cpu = 1,
        .dst_cpu = 12,
        .comm = {'w', 'o', 'r', 'k', 'e', 'r'},
    };
    recording.events.addMigration(migration);

    recording.events.addPmuSample(PmuSample{
        .timestamp_ns = 1'000,
        .tid = 42,
        .cpu_id = 1,
        .instructions = 10,
        .cycles = 20,
        .llc_misses = 30,
        .llc_references = 40,
        .branch_misses = 50,
    });
    return recording;
}

/**
 *  Builds the JSON text of a recording with the given migrations and samples arrays.
 */
auto recordingJson(std::string_view migrations, std::string_view samples) -> std::string
{
    return std::string(R"({"format":"threveal-recording","version":1,"duration_ns":1,)"
                       R"("topology":{"p_cores":[0],"e_cores":[12]},"migrations":)") +
           std::string(migrations) + R"(,"samples":)" + std::string(samples) + "}";
}

/**
 *  A temporary directory that is removed with everything in it when it goes out of scope.
 */
class ScopedTempDir
{
  public:
    ScopedTempDir()
        : path_(std::filesystem::temp_directory_path() /
                ("threveal-test-" + std::to_string(getpid())))
    {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    ScopedTempDir(const ScopedTempDir&) = delete;
    ScopedTempDir(ScopedTempDir&&) = delete;
    auto operator=(const ScopedTempDir&) -> ScopedTempDir& = delete;
    auto operator=(ScopedTempDir&&) -> ScopedTempDir& = delete;

    ~ScopedTempDir()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] auto path() const -> const std::filesystem::path&
    {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

}  // namespace

TEST_CASE("Recording round-trips through JSON", "[storage][recording]")
{
    auto parsed = parseRecording(serializeRecording(makeRecording()));
    REQUIRE(parsed.has_value());

    REQUIRE(parsed->duration_ns == 5'000'000'000);
    REQUIRE(parsed->topology.getPCores().size() == 3);
    REQUIRE(parsed->topology.getECores().size() == 2);
    REQUIRE(parsed->topology.getECores()[0] == 12);

    REQUIRE(parsed->events.migrationCount() == 1);
    const auto& migration = parsed->events.allMigrations()[0];
    REQUIRE(migration.timestamp_ns == 2'000);
    REQUIRE(migration.pid == 7);
    REQUIRE(migration.tid == 42);
    REQUIRE(migration.src_cpu == 1);
    REQUIRE(migration.dst_cpu == 12);
    REQUIRE(migration.commAsStringView() == "worker");

    REQUIRE(parsed->events.pmuSampleCount() == 1);
    const auto& sample = parsed->events.allPmuSamples()[0];
    REQUIRE(sample.timestamp_ns == 1'000);
    REQUIRE(sample.tid == 42);
    REQUIRE(sample.cpu_id == 1);
    REQUIRE(sample.instructions == 10);
    REQUIRE(sample.cycles == 20);
    REQUIRE(sample.llc_misses == 30);
    REQUIRE(sample.llc_references == 40);
    REQUIRE(sample.branch_misses == 50);
}

TEST_CASE("Recording keeps events in time order when loaded", "[storage][recording]")
{
    auto sample = [](std::uint64_t timestamp_ns)
    {
        return R"({"timestamp_ns":)" + std::to_string(timestamp_ns) +
               R"(,"tid":1,"cpu_id":0,"instructions":0,"cycles":0,"llc_misses":0,)"
               R"("llc_references":0,"branch_misses":0})";
    };

    auto parsed = parseRecording(recordingJson("[]", "[" + sample(300) + "," + sample(100) + "]"));
    REQUIRE(parsed.has_value());

    REQUIRE(parsed->events.allPmuSamples()[0].timestamp_ns == 100);
    REQUIRE(parsed->events.allPmuSamples()[1].timestamp_ns == 300);
}

TEST_CASE("Recording truncates a thread name longer than the kernel allows", "[storage][recording]")
{
    auto parsed = parseRecording(recordingJson(
        R"([{"timestamp_ns":1,"pid":1,"tid":1,"src_cpu":0,"dst_cpu":12,"comm":"a-very-long-thread-name"}])",
        "[]"));
    REQUIRE(parsed.has_value());

    REQUIRE(parsed->events.allMigrations()[0].commAsStringView() == "a-very-long-thr");
}

TEST_CASE("Recording rejects malformed input", "[storage][recording]")
{
    auto migration = [](std::string_view tid)
    {
        return R"([{"timestamp_ns":1,"pid":1,"tid":)" + std::string(tid) +
               R"(,"src_cpu":0,"dst_cpu":12,"comm":"x"}])";
    };

    SECTION("not JSON")
    {
        REQUIRE(parseRecording("{not json").error() == RecordingError::kMalformed);
    }

    SECTION("missing field")
    {
        REQUIRE(parseRecording(R"({"format":"threveal-recording","version":1})").error() ==
                RecordingError::kMalformed);
    }

    SECTION("negative number")
    {
        REQUIRE(parseRecording(recordingJson(migration("-1"), "[]")).error() ==
                RecordingError::kMalformed);
    }

    SECTION("number too large for its field")
    {
        REQUIRE(parseRecording(recordingJson(migration("4294967296"), "[]")).error() ==
                RecordingError::kMalformed);
    }

    SECTION("fractional number")
    {
        REQUIRE(parseRecording(recordingJson(migration("1.5"), "[]")).error() ==
                RecordingError::kMalformed);
    }

    SECTION("number written as a string")
    {
        REQUIRE(parseRecording(recordingJson(migration("\"1\""), "[]")).error() ==
                RecordingError::kMalformed);
    }
}

TEST_CASE("Recording rejects other formats and versions", "[storage][recording]")
{
    auto document = recordingJson("[]", "[]");

    SECTION("future version")
    {
        auto future = document;
        future.replace(future.find(R"("version":1)"), 11, R"("version":2)");
        REQUIRE(parseRecording(future).error() == RecordingError::kUnsupportedVersion);
    }

    SECTION("different format")
    {
        auto other = document;
        other.replace(other.find("threveal-recording"), 18, "something-else");
        REQUIRE(parseRecording(other).error() == RecordingError::kUnsupportedVersion);
    }
}

TEST_CASE("Recording files round-trip through disk", "[storage][recording]")
{
    ScopedTempDir dir;
    auto path = dir.path() / "session.json";

    REQUIRE(saveRecording(makeRecording(), path).has_value());

    auto loaded = loadRecording(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->duration_ns == 5'000'000'000);
    REQUIRE(loaded->events.migrationCount() == 1);
    REQUIRE(loaded->events.pmuSampleCount() == 1);
}

TEST_CASE("Recording files replace an existing file", "[storage][recording]")
{
    ScopedTempDir dir;
    auto path = dir.path() / "session.json";

    auto recording = makeRecording();
    REQUIRE(saveRecording(recording, path).has_value());
    recording.duration_ns = 42;
    REQUIRE(saveRecording(recording, path).has_value());

    REQUIRE(loadRecording(path)->duration_ns == 42);
    REQUIRE_FALSE(std::filesystem::exists(dir.path() / "session.json.tmp"));
}

TEST_CASE("Recording files replace a temporary file left by an interrupted save",
          "[storage][recording]")
{
    ScopedTempDir dir;
    auto path = dir.path() / "session.json";
    std::ofstream(dir.path() / "session.json.tmp") << "partial recording";

    REQUIRE(saveRecording(makeRecording(), path).has_value());

    REQUIRE(loadRecording(path).has_value());
}

TEST_CASE("Recording files report why they cannot be read or written", "[storage][recording]")
{
    ScopedTempDir dir;

    SECTION("missing file")
    {
        REQUIRE(loadRecording(dir.path() / "missing.json").error() == RecordingError::kCannotRead);
    }

    SECTION("directory instead of a file")
    {
        REQUIRE(loadRecording(dir.path()).error() == RecordingError::kCannotRead);
    }

    SECTION("device instead of a file")
    {
        REQUIRE(loadRecording("/dev/null").error() == RecordingError::kCannotRead);
    }

    SECTION("file without read permission")
    {
        if (geteuid() == 0)
        {
            SKIP("root ignores file permissions");
        }

        auto path = dir.path() / "private.json";
        REQUIRE(saveRecording(makeRecording(), path).has_value());
        std::filesystem::permissions(path, std::filesystem::perms::none);
        REQUIRE(loadRecording(path).error() == RecordingError::kCannotRead);
    }

    SECTION("file that is not a recording")
    {
        auto path = dir.path() / "garbage.json";
        std::ofstream(path) << "not a recording";
        REQUIRE(loadRecording(path).error() == RecordingError::kMalformed);
    }

    SECTION("directory that does not exist")
    {
        auto path = dir.path() / "missing" / "session.json";
        REQUIRE(saveRecording(makeRecording(), path).error() == RecordingError::kCannotWrite);
    }
}

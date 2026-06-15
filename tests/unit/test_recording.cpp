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
#include <string>
#include <string_view>
#include <vector>

using threveal::core::CpuId;
using threveal::core::MigrationEvent;
using threveal::core::PmuSample;
using threveal::core::TopologyMap;
using threveal::storage::parseRecording;
using threveal::storage::Recording;
using threveal::storage::RecordingError;
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

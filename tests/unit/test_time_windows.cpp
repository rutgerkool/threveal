/**
 *  @file       test_time_windows.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Unit tests for per-thread time windows.
 */

#include "threveal/analysis/time_windows.hpp"
#include "threveal/core/events.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

using Catch::Approx;
using threveal::analysis::buildThreadWindows;
using threveal::analysis::TimedMigration;
using threveal::core::MigrationType;
using threveal::core::PmuSample;

namespace
{

/**
 *  Builds a sample with only instructions and cycles, for testing IPC.
 */
auto makeIpcSample(std::uint64_t timestamp_ns, std::uint64_t instructions, std::uint64_t cycles)
    -> PmuSample
{
    return PmuSample{
        .timestamp_ns = timestamp_ns,
        .tid = 42,
        .cpu_id = 0,
        .instructions = instructions,
        .cycles = cycles,
        .llc_misses = 0,
        .llc_references = 0,
        .branch_misses = 0,
    };
}

/**
 *  Intervals with IPC 2.0, 1.0 and 1.0.
 */
const std::vector<PmuSample> kSamples = {
    makeIpcSample(0, 0, 0),
    makeIpcSample(100, 200, 100),
    makeIpcSample(200, 100, 100),
    makeIpcSample(300, 100, 100),
};

}  // namespace

TEST_CASE("buildThreadWindows splits the range into windows", "[analysis][time_windows]")
{
    SECTION("windows of equal length")
    {
        auto windows = buildThreadWindows(kSamples, {}, 0, 300, 100);

        REQUIRE(windows.size() == 3);
        REQUIRE(windows[0].start_ns == 0);
        REQUIRE(windows[0].end_ns == 100);
        REQUIRE(windows[2].start_ns == 200);
        REQUIRE(windows[2].end_ns == 300);
    }

    SECTION("last window is shorter")
    {
        auto windows = buildThreadWindows(kSamples, {}, 0, 250, 100);

        REQUIRE(windows.size() == 3);
        REQUIRE(windows[2].start_ns == 200);
        REQUIRE(windows[2].end_ns == 250);
    }
}

TEST_CASE("buildThreadWindows counts migrations per window by type", "[analysis][time_windows]")
{
    std::vector<TimedMigration> migrations = {
        {.timestamp_ns = 50, .type = MigrationType::kPToE},
        {.timestamp_ns = 100, .type = MigrationType::kEToP},
        {.timestamp_ns = 150, .type = MigrationType::kPToE},
        {.timestamp_ns = 250, .type = MigrationType::kEToE},
        {.timestamp_ns = 260, .type = MigrationType::kPToP},
        {.timestamp_ns = 300, .type = MigrationType::kPToE},
    };

    auto windows = buildThreadWindows(kSamples, migrations, 0, 300, 100);
    REQUIRE(windows.size() == 3);

    REQUIRE(windows[0].p_to_e_migrations == 1);
    REQUIRE(windows[0].e_to_p_migrations == 0);

    SECTION("a migration on a window edge counts in the later window")
    {
        REQUIRE(windows[1].e_to_p_migrations == 1);
        REQUIRE(windows[1].p_to_e_migrations == 1);
    }

    SECTION("every migration type is counted")
    {
        REQUIRE(windows[2].e_to_e_migrations == 1);
        REQUIRE(windows[2].p_to_p_migrations == 1);
    }

    SECTION("a migration at the end of the range is outside it")
    {
        REQUIRE(windows[2].p_to_e_migrations == 0);
    }
}

TEST_CASE("buildThreadWindows measures the rates in each window", "[analysis][time_windows]")
{
    SECTION("windows aligned with the samples")
    {
        auto windows = buildThreadWindows(kSamples, {}, 0, 300, 100);

        REQUIRE(windows[0].rates.has_value());
        REQUIRE(windows[0].rates->ipc == Approx(2.0));
        REQUIRE(windows[1].rates->ipc == Approx(1.0));
    }

    SECTION("a window straddling two intervals")
    {
        auto windows = buildThreadWindows(kSamples, {}, 50, 150, 100);

        REQUIRE(windows.size() == 1);
        REQUIRE(windows[0].rates.has_value());
        REQUIRE(windows[0].rates->ipc == Approx(1.5));
    }

    SECTION("a window without samples has no rates")
    {
        auto windows = buildThreadWindows(kSamples, {}, 300, 400, 100);

        REQUIRE(windows.size() == 1);
        REQUIRE_FALSE(windows[0].rates.has_value());
    }
}

TEST_CASE("buildThreadWindows returns no windows for an empty request", "[analysis][time_windows]")
{
    REQUIRE(buildThreadWindows(kSamples, {}, 0, 300, 0).empty());
    REQUIRE(buildThreadWindows(kSamples, {}, 300, 300, 100).empty());
    REQUIRE(buildThreadWindows(kSamples, {}, 300, 200, 100).empty());
}

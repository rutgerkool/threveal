/**
 *  @file       test_pmu_group.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Unit tests for PmuGroup.
 *
 *  Note: Many PMU operations require CAP_PERFMON or perf_event_paranoid <= 1.
 *  Tests that require privileges will be skipped if permissions are insufficient.
 */

#include "threveal/collection/pmu_group.hpp"
#include "threveal/core/errors.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstdint>
#include <fstream>
#include <utility>

using Catch::Matchers::WithinRel;
using threveal::collection::PmuGroup;
using threveal::collection::PmuGroupReading;
using threveal::core::PmuError;

namespace
{

/**
 *  Checks if PMU access is permitted on this system.
 */
auto hasPmuAccess() -> bool
{
    std::ifstream file("/proc/sys/kernel/perf_event_paranoid");
    if (!file)
    {
        return false;
    }

    int level = 0;
    file >> level;

    return level <= 1;
}

}  // namespace

TEST_CASE("PmuGroupReading IPC calculation", "[collection][PmuGroupReading]")
{
    SECTION("normal IPC calculation")
    {
        PmuGroupReading reading{
            .cycles = 1000000,
            .instructions = 2000000,
            .llc_loads = 0,
            .llc_load_misses = 0,
            .branch_misses = 0,
        };

        REQUIRE_THAT(reading.ipc(), WithinRel(2.0, 0.001));
    }

    SECTION("zero cycles returns zero IPC")
    {
        PmuGroupReading reading{
            .cycles = 0,
            .instructions = 1000,
            .llc_loads = 0,
            .llc_load_misses = 0,
            .branch_misses = 0,
        };

        REQUIRE(reading.ipc() == 0.0);
    }
}

TEST_CASE("PmuGroupReading LLC miss rate calculation", "[collection][PmuGroupReading]")
{
    SECTION("normal miss rate calculation")
    {
        PmuGroupReading reading{
            .cycles = 0,
            .instructions = 0,
            .llc_loads = 1000,
            .llc_load_misses = 100,
            .branch_misses = 0,
        };

        REQUIRE_THAT(reading.llcMissRate(), WithinRel(0.1, 0.001));
    }

    SECTION("zero loads returns zero miss rate")
    {
        PmuGroupReading reading{
            .cycles = 0,
            .instructions = 0,
            .llc_loads = 0,
            .llc_load_misses = 100,
            .branch_misses = 0,
        };

        REQUIRE(reading.llcMissRate() == 0.0);
    }
}

TEST_CASE("PmuGroup creation requires permissions", "[collection][PmuGroup]")
{
    auto group = PmuGroup::create();

    if (!hasPmuAccess())
    {
        REQUIRE_FALSE(group.has_value());
        REQUIRE(group.error() == PmuError::kPermissionDenied);
    }
    else
    {
        // May still fail if LLC events not supported
        if (group.has_value())
        {
            REQUIRE(group->isValid());
        }
        else
        {
            // LLC events may not be supported on all hardware
            REQUIRE((group.error() == PmuError::kEventNotSupported ||
                     group.error() == PmuError::kTooManyEvents));
        }
    }
}

TEST_CASE("PmuGroup move semantics", "[collection][PmuGroup]")
{
    if (!hasPmuAccess())
    {
        SKIP("PMU access not permitted (perf_event_paranoid > 1)");
    }

    auto group1 = PmuGroup::create();
    if (!group1.has_value())
    {
        SKIP("PMU group creation failed (LLC events may not be supported)");
    }

    REQUIRE(group1->isValid());

    // Move construct
    PmuGroup group2 = std::move(*group1);
    REQUIRE(group2.isValid());
    REQUIRE_FALSE(group1->isValid());

    // Move assign
    auto group3 = PmuGroup::create();
    if (!group3.has_value())
    {
        SKIP("PMU group creation failed");
    }

    *group3 = std::move(group2);
    REQUIRE(group3->isValid());
    REQUIRE_FALSE(group2.isValid());
}

TEST_CASE("PmuGroup enable/disable/reset", "[collection][PmuGroup]")
{
    if (!hasPmuAccess())
    {
        SKIP("PMU access not permitted (perf_event_paranoid > 1)");
    }

    auto group = PmuGroup::create();
    if (!group.has_value())
    {
        SKIP("PMU group creation failed (LLC events may not be supported)");
    }

    SECTION("enable succeeds")
    {
        auto result = group->enable();
        REQUIRE(result.has_value());
    }

    SECTION("disable succeeds")
    {
        auto enable_result = group->enable();
        REQUIRE(enable_result.has_value());

        auto result = group->disable();
        REQUIRE(result.has_value());
    }

    SECTION("reset succeeds")
    {
        auto result = group->reset();
        REQUIRE(result.has_value());
    }
}

TEST_CASE("PmuGroup read returns values", "[collection][PmuGroup]")
{
    if (!hasPmuAccess())
    {
        SKIP("PMU access not permitted (perf_event_paranoid > 1)");
    }

    auto group = PmuGroup::create();
    if (!group.has_value())
    {
        SKIP("PMU group creation failed (LLC events may not be supported)");
    }

    auto enable_result = group->enable();
    REQUIRE(enable_result.has_value());

    // Do some work to accumulate events
    volatile std::uint64_t sum = 0;
    for (std::uint64_t i = 0; i < 100000; ++i)
    {
        sum += i;
    }
    (void)sum;

    auto disable_result = group->disable();
    REQUIRE(disable_result.has_value());

    auto reading = group->read();
    REQUIRE(reading.has_value());
    REQUIRE(reading->cycles > 0);
    REQUIRE(reading->instructions > 0);
}

TEST_CASE("PmuGroup operations on invalid group fail", "[collection][PmuGroup]")
{
    if (!hasPmuAccess())
    {
        SKIP("PMU access not permitted (perf_event_paranoid > 1)");
    }

    auto group = PmuGroup::create();
    if (!group.has_value())
    {
        SKIP("PMU group creation failed");
    }

    // Move the group to invalidate it
    PmuGroup moved = std::move(*group);

    SECTION("read on invalid group fails")
    {
        auto result = group->read();
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == PmuError::kInvalidState);
    }

    SECTION("enable on invalid group fails")
    {
        auto result = group->enable();
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == PmuError::kInvalidState);
    }

    SECTION("disable on invalid group fails")
    {
        auto result = group->disable();
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == PmuError::kInvalidState);
    }

    SECTION("reset on invalid group fails")
    {
        auto result = group->reset();
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == PmuError::kInvalidState);
    }
}

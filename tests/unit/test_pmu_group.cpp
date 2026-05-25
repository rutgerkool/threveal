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
#include "threveal/core/topology.hpp"
#include "threveal/core/types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstdint>
#include <utility>

#include "pmu_test_support.hpp"

using Catch::Matchers::WithinRel;
using threveal::collection::PmuGroup;
using threveal::collection::PmuGroupReading;
using threveal::core::CpuId;
using threveal::core::PmuError;
using threveal::core::TopologyMap;
using threveal::test::burnCycles;
using threveal::test::hasPmuAccess;
using threveal::test::ScopedCpuPin;

namespace
{

/**
 *  Reads the group while pinned to one CPU and requires non-zero cycles and instructions.
 */
void requireGroupCountsOn(CpuId cpu)
{
    ScopedCpuPin pin(cpu);
    REQUIRE(pin.pinned());

    auto group = PmuGroup::create();
    REQUIRE(group.has_value());

    REQUIRE(group->enable().has_value());
    burnCycles();
    REQUIRE(group->disable().has_value());

    auto reading = group->read();
    REQUIRE(reading.has_value());
    REQUIRE(reading->cycles > 0);
    REQUIRE(reading->instructions > 0);
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

TEST_CASE("PmuGroupReading since returns the per-counter difference",
          "[collection][PmuGroupReading]")
{
    PmuGroupReading earlier{
        .cycles = 1000,
        .instructions = 2000,
        .llc_loads = 300,
        .llc_load_misses = 40,
        .branch_misses = 5,
    };
    PmuGroupReading later{
        .cycles = 1600,
        .instructions = 3200,
        .llc_loads = 370,
        .llc_load_misses = 49,
        .branch_misses = 8,
    };

    auto delta = later.since(earlier);

    REQUIRE(delta.cycles == 600);
    REQUIRE(delta.instructions == 1200);
    REQUIRE(delta.llc_loads == 70);
    REQUIRE(delta.llc_load_misses == 9);
    REQUIRE(delta.branch_misses == 3);
}

TEST_CASE("PmuGroupReading since itself is zero", "[collection][PmuGroupReading]")
{
    PmuGroupReading reading{
        .cycles = 1000,
        .instructions = 2000,
        .llc_loads = 300,
        .llc_load_misses = 40,
        .branch_misses = 5,
    };

    auto delta = reading.since(reading);

    REQUIRE(delta.cycles == 0);
    REQUIRE(delta.instructions == 0);
    REQUIRE(delta.llc_loads == 0);
    REQUIRE(delta.llc_load_misses == 0);
    REQUIRE(delta.branch_misses == 0);
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

    PmuGroup group2 = std::move(*group1);
    REQUIRE(group2.isValid());
    REQUIRE_FALSE(group1->isValid());

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

TEST_CASE("PmuGroup counts on both core types of a hybrid CPU", "[collection][PmuGroup]")
{
    if (!hasPmuAccess())
    {
        SKIP("PMU access not permitted (perf_event_paranoid > 1)");
    }

    auto topology = TopologyMap::loadFromSysfs();
    if (!topology.has_value() || !topology->isHybrid())
    {
        SKIP("Requires a hybrid CPU");
    }

    SECTION("P-core")
    {
        requireGroupCountsOn(topology->getPCores().front());
    }

    SECTION("E-core")
    {
        requireGroupCountsOn(topology->getECores().front());
    }
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

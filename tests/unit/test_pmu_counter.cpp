/**
 *  @file       test_pmu_counter.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Unit tests for PmuCounter.
 *
 *  Note: Many PMU operations require CAP_PERFMON or perf_event_paranoid <= 1.
 *  Tests that require privileges will be skipped if permissions are insufficient.
 */

#include "threveal/collection/pmu_counter.hpp"
#include "threveal/core/errors.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <fstream>
#include <utility>

using threveal::collection::PmuCounter;
using threveal::collection::PmuEventType;
using threveal::collection::toString;
using threveal::core::PmuError;

namespace
{

/**
 *  Checks if PMU access is permitted on this system.
 *
 *  @return     True if perf_event_paranoid allows user-space PMU access.
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

    // Level <= 1 allows user-space PMU access without CAP_PERFMON
    return level <= 1;
}

}  // namespace

TEST_CASE("PmuEventType toString", "[collection][PmuEventType]")
{
    REQUIRE(toString(PmuEventType::kCycles) == "cycles");
    REQUIRE(toString(PmuEventType::kInstructions) == "instructions");
    REQUIRE(toString(PmuEventType::kLlcLoads) == "LLC-loads");
    REQUIRE(toString(PmuEventType::kLlcLoadMisses) == "LLC-load-misses");
    REQUIRE(toString(PmuEventType::kBranchMisses) == "branch-misses");
}

TEST_CASE("PmuCounter creation requires permissions", "[collection][PmuCounter]")
{
    auto counter = PmuCounter::create(PmuEventType::kCycles);

    if (!hasPmuAccess())
    {
        // Without permissions, creation should fail
        REQUIRE_FALSE(counter.has_value());
        REQUIRE(counter.error() == PmuError::kPermissionDenied);
    }
    else
    {
        // With permissions, creation should succeed
        REQUIRE(counter.has_value());
        REQUIRE(counter->isValid());
        REQUIRE(counter->eventType() == PmuEventType::kCycles);
    }
}

TEST_CASE("PmuCounter invalid target", "[collection][PmuCounter]")
{
    // Try to create counter for non-existent process
    auto counter = PmuCounter::create(PmuEventType::kCycles, 999999999);

    REQUIRE_FALSE(counter.has_value());
    // Should get either permission denied or invalid target depending on check order
    REQUIRE((counter.error() == PmuError::kInvalidTarget ||
             counter.error() == PmuError::kPermissionDenied));
}

TEST_CASE("PmuCounter move semantics", "[collection][PmuCounter]")
{
    if (!hasPmuAccess())
    {
        SKIP("PMU access not permitted (perf_event_paranoid > 1)");
    }

    auto counter1 = PmuCounter::create(PmuEventType::kCycles);
    REQUIRE(counter1.has_value());
    REQUIRE(counter1->isValid());

    int original_fd = counter1->fileDescriptor();

    // Move construct
    PmuCounter counter2 = std::move(*counter1);
    REQUIRE(counter2.isValid());
    REQUIRE(counter2.fileDescriptor() == original_fd);
    REQUIRE_FALSE(counter1->isValid());

    // Move assign
    auto counter3 = PmuCounter::create(PmuEventType::kInstructions);
    REQUIRE(counter3.has_value());

    counter3 = std::move(counter2);
    REQUIRE(counter3->isValid());
    REQUIRE(counter3->fileDescriptor() == original_fd);
    REQUIRE_FALSE(counter2.isValid());
}

TEST_CASE("PmuCounter enable/disable/reset", "[collection][PmuCounter]")
{
    if (!hasPmuAccess())
    {
        SKIP("PMU access not permitted (perf_event_paranoid > 1)");
    }

    auto counter = PmuCounter::create(PmuEventType::kCycles);
    REQUIRE(counter.has_value());

    SECTION("enable succeeds")
    {
        auto result = counter->enable();
        REQUIRE(result.has_value());
    }

    SECTION("disable succeeds")
    {
        auto enable_result = counter->enable();
        REQUIRE(enable_result.has_value());

        auto result = counter->disable();
        REQUIRE(result.has_value());
    }

    SECTION("reset succeeds")
    {
        auto result = counter->reset();
        REQUIRE(result.has_value());
    }
}

TEST_CASE("PmuCounter read returns value", "[collection][PmuCounter]")
{
    if (!hasPmuAccess())
    {
        SKIP("PMU access not permitted (perf_event_paranoid > 1)");
    }

    auto counter = PmuCounter::create(PmuEventType::kCycles);
    REQUIRE(counter.has_value());

    // Enable counter
    auto enable_result = counter->enable();
    REQUIRE(enable_result.has_value());

    // Do some work to accumulate cycles
    volatile std::uint64_t sum = 0;
    for (std::uint64_t i = 0; i < 100000; ++i)
    {
        sum += i;
    }
    (void)sum;

    // Disable and read
    auto disable_result = counter->disable();
    REQUIRE(disable_result.has_value());

    auto value = counter->read();
    REQUIRE(value.has_value());
    REQUIRE(*value > 0);
}

TEST_CASE("PmuCounter all event types can be created", "[collection][PmuCounter]")
{
    if (!hasPmuAccess())
    {
        SKIP("PMU access not permitted (perf_event_paranoid > 1)");
    }

    SECTION("cycles")
    {
        auto counter = PmuCounter::create(PmuEventType::kCycles);
        REQUIRE(counter.has_value());
    }

    SECTION("instructions")
    {
        auto counter = PmuCounter::create(PmuEventType::kInstructions);
        REQUIRE(counter.has_value());
    }

    SECTION("branch misses")
    {
        auto counter = PmuCounter::create(PmuEventType::kBranchMisses);
        REQUIRE(counter.has_value());
    }

    SECTION("LLC loads")
    {
        auto counter = PmuCounter::create(PmuEventType::kLlcLoads);
        // LLC events may not be supported on all hardware
        if (!counter.has_value())
        {
            REQUIRE(counter.error() == PmuError::kEventNotSupported);
        }
    }

    SECTION("LLC load misses")
    {
        auto counter = PmuCounter::create(PmuEventType::kLlcLoadMisses);
        // LLC events may not be supported on all hardware
        if (!counter.has_value())
        {
            REQUIRE(counter.error() == PmuError::kEventNotSupported);
        }
    }
}

TEST_CASE("PmuCounter operations on invalid counter fail", "[collection][PmuCounter]")
{
    if (!hasPmuAccess())
    {
        SKIP("PMU access not permitted (perf_event_paranoid > 1)");
    }

    auto counter = PmuCounter::create(PmuEventType::kCycles);
    REQUIRE(counter.has_value());

    // Move the counter to invalidate it
    PmuCounter moved = std::move(*counter);

    SECTION("read on invalid counter fails")
    {
        auto result = counter->read();
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == PmuError::kInvalidState);
    }

    SECTION("enable on invalid counter fails")
    {
        auto result = counter->enable();
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == PmuError::kInvalidState);
    }

    SECTION("disable on invalid counter fails")
    {
        auto result = counter->disable();
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == PmuError::kInvalidState);
    }

    SECTION("reset on invalid counter fails")
    {
        auto result = counter->reset();
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == PmuError::kInvalidState);
    }
}

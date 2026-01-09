/**
 *  @file       test_errors.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Unit tests for error types.
 */

#include "threveal/core/errors.hpp"

#include <catch2/catch_test_macros.hpp>

using threveal::core::PmuError;
using threveal::core::TopologyError;
using threveal::core::toString;

TEST_CASE("TopologyError toString", "[errors][TopologyError]")
{
    REQUIRE(toString(TopologyError::kSysfsNotFound) == "sysfs topology entries not found");
    REQUIRE(toString(TopologyError::kNotHybridCpu) == "system does not have a hybrid CPU");
    REQUIRE(toString(TopologyError::kParseError) == "failed to parse CPU list format");
    REQUIRE(toString(TopologyError::kInvalidCpuId) == "invalid CPU ID");
    REQUIRE(toString(TopologyError::kPermissionDenied) == "permission denied accessing sysfs");
}

TEST_CASE("PmuError toString", "[errors][PmuError]")
{
    REQUIRE(toString(PmuError::kOpenFailed) == "perf_event_open() failed");
    REQUIRE(toString(PmuError::kReadFailed) == "failed to read PMU counter");
    REQUIRE(toString(PmuError::kEventNotSupported) == "PMU event not supported on this hardware");
    REQUIRE(toString(PmuError::kPermissionDenied) == "permission denied for PMU access");
    REQUIRE(toString(PmuError::kInvalidTarget) == "invalid thread or process ID");
    REQUIRE(toString(PmuError::kTooManyEvents) == "too many PMU events for available counters");
    REQUIRE(toString(PmuError::kInvalidState) == "PMU counter in invalid state");
}

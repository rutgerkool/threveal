/**
 *  @file       errors.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Error types for the Threveal profiler.
 *
 *  Defines error enumerations used throughout Threveal for representing
 *  failure conditions in topology detection and other subsystems.
 */

#ifndef THREVEAL_CORE_ERRORS_HPP_
#define THREVEAL_CORE_ERRORS_HPP_

#include <cstdint>
#include <string_view>

namespace threveal::core
{

/**
 *  Error conditions that can occur during CPU topology detection.
 *
 *  These errors are returned via std::expected from topology detection
 *  functions when the operation cannot complete successfully.
 */
enum class TopologyError : std::uint8_t
{
    /**
     *  The sysfs filesystem entries for CPU topology were not found.
     *
     *  This typically indicates /sys/devices/cpu_core/cpus is not accessible.
     */
    kSysfsNotFound = 1,

    /**
     *  The system does not have a hybrid CPU architecture.
     *
     *  This occurs when cpu_core exists but cpu_atom does not, indicating
     *  a homogeneous CPU without efficiency cores.
     */
    kNotHybridCpu = 2,

    /**
     *  The sysfs content could not be parsed.
     *
     *  The CPU list format (e.g., "0-5,12-19") was malformed or unexpected.
     */
    kParseError = 3,

    /**
     *  The requested CPU ID is not valid for this system.
     *
     *  The CPU ID is outside the range of detected CPUs.
     */
    kInvalidCpuId = 4,

    /**
     *  Permission was denied when accessing sysfs entries.
     */
    kPermissionDenied = 5,
};

/**
 *  Converts a TopologyError to its human-readable string representation.
 *
 *  @param      error  The error to convert.
 *  @return     A string view describing the error condition.
 */
[[nodiscard]] constexpr auto toString(TopologyError error) noexcept -> std::string_view
{
    switch (error)
    {
        case TopologyError::kSysfsNotFound:
            return "sysfs topology entries not found";
        case TopologyError::kNotHybridCpu:
            return "system does not have a hybrid CPU";
        case TopologyError::kParseError:
            return "failed to parse CPU list format";
        case TopologyError::kInvalidCpuId:
            return "invalid CPU ID";
        case TopologyError::kPermissionDenied:
            return "permission denied accessing sysfs";
    }
    return "unknown topology error";
}

/**
 *  Error conditions that can occur during PMU (Performance Monitoring Unit) operations.
 *
 *  These errors are returned via std::expected from PMU collection functions
 *  when hardware performance counter operations fail.
 */
enum class PmuError : std::uint8_t
{
    /**
     *  The perf_event_open() system call failed.
     *
     *  This may indicate insufficient permissions (CAP_PERFMON required)
     *  or that the requested event is not available on this CPU.
     */
    kOpenFailed = 1,

    /**
     *  Reading from the perf event file descriptor failed.
     *
     *  The counter may have been disabled or the file descriptor invalidated.
     */
    kReadFailed = 2,

    /**
     *  The requested PMU event is not supported on this hardware.
     *
     *  Some events are CPU-specific or may not be available on hybrid cores.
     */
    kEventNotSupported = 3,

    /**
     *  Permission denied when accessing performance counters.
     *
     *  Check /proc/sys/kernel/perf_event_paranoid or use CAP_PERFMON capability.
     */
    kPermissionDenied = 4,

    /**
     *  The specified thread or process ID is invalid.
     */
    kInvalidTarget = 5,

    /**
     *  Too many PMU events requested for the available hardware counters.
     *
     *  Hardware has limited programmable counters; reduce the event count.
     */
    kTooManyEvents = 6,

    /**
     *  The PMU group or counter is in an invalid state for the operation.
     */
    kInvalidState = 7,
};

/**
 *  Converts a PmuError to its human-readable string representation.
 *
 *  @param      error  The error to convert.
 *  @return     A string view describing the error condition.
 */
[[nodiscard]] constexpr auto toString(PmuError error) noexcept -> std::string_view
{
    switch (error)
    {
        case PmuError::kOpenFailed:
            return "perf_event_open() failed";
        case PmuError::kReadFailed:
            return "failed to read PMU counter";
        case PmuError::kEventNotSupported:
            return "PMU event not supported on this hardware";
        case PmuError::kPermissionDenied:
            return "permission denied for PMU access";
        case PmuError::kInvalidTarget:
            return "invalid thread or process ID";
        case PmuError::kTooManyEvents:
            return "too many PMU events for available counters";
        case PmuError::kInvalidState:
            return "PMU counter in invalid state";
    }
    return "unknown PMU error";
}

}  // namespace threveal::core

#endif  // THREVEAL_CORE_ERRORS_HPP_

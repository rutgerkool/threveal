/**
 *  @file       perf_event.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Shared definitions for perf_event based hardware counters.
 */

#ifndef THREVEAL_COLLECTION_PERF_EVENT_HPP_
#define THREVEAL_COLLECTION_PERF_EVENT_HPP_

#include "threveal/core/errors.hpp"

#include <cstddef>
#include <cstdint>
#include <linux/perf_event.h>
#include <string_view>
#include <sys/types.h>
#include <vector>

namespace threveal::collection
{

/**
 *  Directory where the kernel lists its perf event sources.
 */
inline constexpr std::string_view kEventSourceDir = "/sys/bus/event_source/devices";

/**
 *  PMU type that lets the kernel pick its default PMU for a generic event.
 */
inline constexpr std::uint32_t kDefaultPmuType = 0;

/**
 *  Maximum number of core PMUs a CPU exposes
 */
inline constexpr std::size_t kMaxCorePmus = 2;

/**
 *  Hardware performance counter event types.
 */
enum class PmuEventType : std::uint8_t
{
    /**
     *  CPU cycles elapsed.
     *
     *  Maps to PERF_COUNT_HW_CPU_CYCLES.
     */
    kCycles = 0,

    /**
     *  Instructions retired.
     *
     *  Maps to PERF_COUNT_HW_INSTRUCTIONS.
     */
    kInstructions = 1,

    /**
     *  Last-level cache load references.
     */
    kLlcLoads = 2,

    /**
     *  Last-level cache load misses.
     */
    kLlcLoadMisses = 3,

    /**
     *  Branch mispredictions.
     */
    kBranchMisses = 4,
};

/**
 *  Converts a PmuEventType to its human-readable string representation.
 *
 *  @param      event  The event type to convert.
 *  @return     A string view describing the event.
 */
[[nodiscard]] constexpr auto toString(PmuEventType event) noexcept -> std::string_view
{
    switch (event)
    {
        case PmuEventType::kCycles:
            return "cycles";
        case PmuEventType::kInstructions:
            return "instructions";
        case PmuEventType::kLlcLoads:
            return "LLC-loads";
        case PmuEventType::kLlcLoadMisses:
            return "LLC-load-misses";
        case PmuEventType::kBranchMisses:
            return "branch-misses";
    }
    return "unknown";
}

/**
 *  Wrapper for the perf_event_open syscall.
 *
 *  @param      attr      Pointer to perf_event_attr configuration structure.
 *  @param      pid       Process/thread ID to monitor (0 for calling thread).
 *  @param      cpu       CPU to monitor (-1 for any CPU the thread runs on).
 *  @param      group_fd  File descriptor of group leader (-1 for new group).
 *  @param      flags     Additional flags (usually 0).
 *  @return     File descriptor on success, -1 on error with errno set.
 */
[[nodiscard]] auto perfEventOpen(perf_event_attr* attr, pid_t pid, int cpu, int group_fd,
                                 unsigned long flags) -> int;

/**
 *  Creates a perf_event_attr for the given PmuEventType.
 *
 *  @param      event     The PMU event type to configure.
 *  @param      pmu_type  The PMU that should count the event.
 *  @return     Configured perf_event_attr structure for the requested event.
 */
[[nodiscard]] auto makeEventAttr(PmuEventType event, std::uint32_t pmu_type = kDefaultPmuType)
    -> perf_event_attr;

/**
 *  Maps errno values from perf_event_open() to PmuError.
 *
 *  @param      err  The errno value to translate.
 *  @return     The corresponding PmuError value.
 */
[[nodiscard]] auto errnoToPmuError(int err) noexcept -> core::PmuError;

/**
 *  Detects the PMU types that per-thread hardware counters must be opened on.
 *
 *  @param      event_source_dir  Directory listing the perf event sources.
 *  @return     The PMU types to open counters on, never empty.
 */
[[nodiscard]] auto detectCorePmuTypes(std::string_view event_source_dir = kEventSourceDir)
    -> std::vector<std::uint32_t>;

}  // namespace threveal::collection

#endif  // THREVEAL_COLLECTION_PERF_EVENT_HPP_

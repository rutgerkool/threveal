/**
 *  @file       perf_event.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Shared definitions for perf_event based hardware counters.
 */

#ifndef THREVEAL_COLLECTION_PERF_EVENT_HPP_
#define THREVEAL_COLLECTION_PERF_EVENT_HPP_

#include "threveal/core/errors.hpp"

#include <cstdint>
#include <linux/perf_event.h>
#include <string_view>
#include <sys/types.h>

namespace threveal::collection
{

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
     *
     *  Maps to PERF_COUNT_HW_CACHE_LL | PERF_COUNT_HW_CACHE_OP_READ | ACCESS.
     */
    kLlcLoads = 2,

    /**
     *  Last-level cache load misses.
     *
     *  Maps to PERF_COUNT_HW_CACHE_LL | PERF_COUNT_HW_CACHE_OP_READ | MISS.
     */
    kLlcLoadMisses = 3,

    /**
     *  Branch mispredictions.
     *
     *  Maps to PERF_COUNT_HW_BRANCH_MISSES.
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
 *  @param      event  The PMU event type to configure.
 *  @return     Configured perf_event_attr structure for the requested event.
 */
[[nodiscard]] auto makeEventAttr(PmuEventType event) -> perf_event_attr;

/**
 *  Maps errno values from perf_event_open() to PmuError.
 *
 *  @param      err  The errno value to translate.
 *  @return     The corresponding PmuError value.
 */
[[nodiscard]] auto errnoToPmuError(int err) noexcept -> core::PmuError;

}  // namespace threveal::collection

#endif  // THREVEAL_COLLECTION_PERF_EVENT_HPP_

/**
 *  @file       perf_event.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Shared definitions for perf_event based hardware counters.
 */

#ifndef THREVEAL_COLLECTION_PERF_EVENT_HPP_
#define THREVEAL_COLLECTION_PERF_EVENT_HPP_

#include <cstdint>
#include <string_view>

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

}  // namespace threveal::collection

#endif  // THREVEAL_COLLECTION_PERF_EVENT_HPP_

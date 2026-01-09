/**
 *  @file       pmu_counter.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  RAII wrapper for Linux perf_event hardware performance counters.
 *
 *  Provides a safe interface for opening, reading, and managing individual
 *  PMU (Performance Monitoring Unit) counters via the perf_event_open() syscall.
 */

#ifndef THREVEAL_COLLECTION_PMU_COUNTER_HPP_
#define THREVEAL_COLLECTION_PMU_COUNTER_HPP_

#include "threveal/core/errors.hpp"

#include <cstdint>
#include <expected>
#include <string_view>
#include <sys/types.h>

namespace threveal::collection
{

/**
 *  Hardware performance counter event types.
 *
 *  These correspond to the PMU events needed for migration impact analysis.
 *  Each event maps to a specific perf_event configuration.
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
 *  RAII wrapper for a single hardware performance counter.
 *
 *  PmuCounter encapsulates a perf_event file descriptor, providing safe
 *  resource management and a type-safe interface for reading counter values.
 *
 *  This class is move-only; file descriptors cannot be safely copied.
 *
 *  Example usage:
 *  @code
 *      auto counter = PmuCounter::create(PmuEventType::kCycles, tid);
 *      if (!counter) {
 *          // Handle error
 *      }
 *      counter->enable();
 *      // ... workload runs ...
 *      auto value = counter->read();
 *  @endcode
 */
class PmuCounter
{
  public:
    /**
     *  Creates a new PMU counter for the specified event and target.
     *
     *  Opens a perf_event file descriptor configured for the given event type.
     *  The counter is created in a disabled state; call enable() to start counting.
     *
     *  @param      event  The type of hardware event to count.
     *  @param      tid    Thread ID to monitor (0 or -1 for calling thread).
     *  @param      cpu    CPU to monitor (-1 for any CPU the thread runs on).
     *  @return     A PmuCounter on success, or PmuError on failure.
     */
    [[nodiscard]] static auto create(PmuEventType event, pid_t tid = 0, int cpu = -1)
        -> std::expected<PmuCounter, core::PmuError>;

    /**
     *  Destroys the counter and closes the file descriptor.
     */
    ~PmuCounter();

    /**
     *  Move constructor.
     *
     *  @param      other  Counter to move from (will be invalidated).
     */
    PmuCounter(PmuCounter&& other) noexcept;

    /**
     *  Move assignment operator.
     *
     *  @param      other  Counter to move from (will be invalidated).
     *  @return     Reference to this counter.
     */
    auto operator=(PmuCounter&& other) noexcept -> PmuCounter&;

    // Non-copyable
    PmuCounter(const PmuCounter&) = delete;
    auto operator=(const PmuCounter&) -> PmuCounter& = delete;

    /**
     *  Reads the current counter value.
     *
     *  Returns the accumulated count since the counter was enabled or last reset.
     *
     *  @return     The counter value on success, or PmuError on failure.
     */
    [[nodiscard]] auto read() const -> std::expected<std::uint64_t, core::PmuError>;

    /**
     *  Resets the counter value to zero.
     *
     *  @return     Success or PmuError on failure.
     */
    [[nodiscard]] auto reset() const -> std::expected<void, core::PmuError>;

    /**
     *  Enables the counter to start accumulating events.
     *
     *  @return     Success or PmuError on failure.
     */
    [[nodiscard]] auto enable() const -> std::expected<void, core::PmuError>;

    /**
     *  Disables the counter, stopping event accumulation.
     *
     *  The counter value is preserved and can still be read.
     *
     *  @return     Success or PmuError on failure.
     */
    [[nodiscard]] auto disable() const -> std::expected<void, core::PmuError>;

    /**
     *  Returns the event type this counter is measuring.
     *
     *  @return     The PMU event type.
     */
    [[nodiscard]] auto eventType() const noexcept -> PmuEventType;

    /**
     *  Returns the underlying file descriptor.
     *
     *  Useful for advanced operations like grouping counters or polling.
     *
     *  @return     The perf_event file descriptor, or -1 if invalid.
     */
    [[nodiscard]] auto fileDescriptor() const noexcept -> int;

    /**
     *  Checks if the counter is in a valid state.
     *
     *  @return     True if the counter has a valid file descriptor.
     */
    [[nodiscard]] auto isValid() const noexcept -> bool;

  private:
    /**
     *  Private constructor - use create() factory method.
     *
     *  @param      fd     The perf_event file descriptor.
     *  @param      event  The event type being counted.
     */
    PmuCounter(int fd, PmuEventType event) noexcept;

    static constexpr int kInvalidFd = -1;

    int fd_;
    PmuEventType event_type_;
};

}  // namespace threveal::collection

#endif  // THREVEAL_COLLECTION_PMU_COUNTER_HPP_

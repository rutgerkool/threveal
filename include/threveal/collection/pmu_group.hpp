/**
 *  @file       pmu_group.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  RAII wrapper for grouped Linux perf_event hardware performance counters.
 *
 *  Provides atomic reading of multiple PMU counters using perf_event groups.
 *  This enables accurate correlation between metrics like IPC and cache misses.
 */

#ifndef THREVEAL_COLLECTION_PMU_GROUP_HPP_
#define THREVEAL_COLLECTION_PMU_GROUP_HPP_

#include "threveal/core/errors.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <sys/types.h>

namespace threveal::collection
{

/**
 *  Results from reading a PMU counter group atomically.
 *
 *  Contains raw counter values and provides computed metrics.
 *  All values represent deltas since the last reset or enable.
 */
struct PmuGroupReading
{
    /**
     *  CPU cycles elapsed.
     */
    std::uint64_t cycles;

    /**
     *  Instructions retired.
     */
    std::uint64_t instructions;

    /**
     *  Last-level cache load references.
     */
    std::uint64_t llc_loads;

    /**
     *  Last-level cache load misses.
     */
    std::uint64_t llc_load_misses;

    /**
     *  Branch mispredictions.
     */
    std::uint64_t branch_misses;

    /**
     *  Computes Instructions Per Cycle (IPC).
     *
     *  @return     IPC value, or 0.0 if cycles is zero.
     */
    [[nodiscard]] constexpr auto ipc() const noexcept -> double
    {
        if (cycles == 0)
        {
            return 0.0;
        }
        return static_cast<double>(instructions) / static_cast<double>(cycles);
    }

    /**
     *  Computes the LLC miss rate.
     *
     *  @return     Miss rate (0.0 to 1.0), or 0.0 if no references.
     */
    [[nodiscard]] constexpr auto llcMissRate() const noexcept -> double
    {
        if (llc_loads == 0)
        {
            return 0.0;
        }
        return static_cast<double>(llc_load_misses) / static_cast<double>(llc_loads);
    }
};

/**
 *  RAII wrapper for a group of hardware performance counters.
 *
 *  PmuGroup creates a perf_event group containing all counters needed for
 *  migration impact analysis: cycles, instructions, LLC loads/misses, and
 *  branch misses. Reading the group returns all values atomically.
 *
 *  This class is move-only; file descriptors cannot be safely copied.
 *
 *  Example usage:
 *  @code
 *      auto group = PmuGroup::create(tid);
 *      if (!group) {
 *          // Handle error
 *      }
 *      group->enable();
 *      // ... workload runs ...
 *      auto reading = group->read();
 *      if (reading) {
 *          fmt::print("IPC: {:.2f}\n", reading->ipc());
 *      }
 *  @endcode
 */
class PmuGroup
{
  public:
    /**
     *  Number of counters in the group.
     */
    static constexpr std::size_t kCounterCount = 5;

    /**
     *  Creates a new PMU counter group for the specified target.
     *
     *  Opens perf_event file descriptors for cycles, instructions, LLC loads,
     *  LLC misses, and branch misses as a group. The group is created disabled;
     *  call enable() to start counting.
     *
     *  @param      tid  Thread ID to monitor (0 for calling thread).
     *  @param      cpu  CPU to monitor (-1 for any CPU the thread runs on).
     *  @return     A PmuGroup on success, or PmuError on failure.
     */
    [[nodiscard]] static auto create(pid_t tid = 0, int cpu = -1)
        -> std::expected<PmuGroup, core::PmuError>;

    /**
     *  Destroys the group and closes all file descriptors.
     */
    ~PmuGroup();

    /**
     *  Move constructor.
     *
     *  @param      other  Group to move from (will be invalidated).
     */
    PmuGroup(PmuGroup&& other) noexcept;

    /**
     *  Move assignment operator.
     *
     *  @param      other  Group to move from (will be invalidated).
     *  @return     Reference to this group.
     */
    auto operator=(PmuGroup&& other) noexcept -> PmuGroup&;

    // Non-copyable
    PmuGroup(const PmuGroup&) = delete;
    auto operator=(const PmuGroup&) -> PmuGroup& = delete;

    /**
     *  Reads all counter values atomically.
     *
     *  Returns the accumulated counts since the group was enabled or last reset.
     *  The read is atomic across all counters in the group.
     *
     *  @return     Counter readings on success, or PmuError on failure.
     */
    [[nodiscard]] auto read() const -> std::expected<PmuGroupReading, core::PmuError>;

    /**
     *  Resets all counter values to zero.
     *
     *  @return     Success or PmuError on failure.
     */
    [[nodiscard]] auto reset() const -> std::expected<void, core::PmuError>;

    /**
     *  Enables all counters to start accumulating events.
     *
     *  @return     Success or PmuError on failure.
     */
    [[nodiscard]] auto enable() const -> std::expected<void, core::PmuError>;

    /**
     *  Disables all counters, stopping event accumulation.
     *
     *  Counter values are preserved and can still be read.
     *
     *  @return     Success or PmuError on failure.
     */
    [[nodiscard]] auto disable() const -> std::expected<void, core::PmuError>;

    /**
     *  Checks if the group is in a valid state.
     *
     *  @return     True if all file descriptors are valid.
     */
    [[nodiscard]] auto isValid() const noexcept -> bool;

  private:
    /**
     *  Private constructor - use create() factory method.
     *
     *  @param      fds  Array of perf_event file descriptors.
     */
    explicit PmuGroup(std::array<int, kCounterCount> fds) noexcept;

    /**
     *  Closes all valid file descriptors.
     */
    void closeAll() noexcept;

    static constexpr int kInvalidFd = -1;

    /**
     *  File descriptors for each counter in the group.
     *  Order: cycles (leader), instructions, llc_loads, llc_load_misses, branch_misses
     */
    std::array<int, kCounterCount> fds_;
};

}  // namespace threveal::collection

#endif  // THREVEAL_COLLECTION_PMU_GROUP_HPP_

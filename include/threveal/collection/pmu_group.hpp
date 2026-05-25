/**
 *  @file       pmu_group.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Wrapper for grouped Linux perf_event hardware performance counters.
 */

#ifndef THREVEAL_COLLECTION_PMU_GROUP_HPP_
#define THREVEAL_COLLECTION_PMU_GROUP_HPP_

#include "threveal/collection/perf_event.hpp"
#include "threveal/core/errors.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <sys/types.h>

namespace threveal::collection
{

/**
 *  Results from reading a PMU counter group atomically.
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
     *  Computes IPC
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
     *  @return     Miss rate, or 0.0 if no references.
     */
    [[nodiscard]] constexpr auto llcMissRate() const noexcept -> double
    {
        if (llc_loads == 0)
        {
            return 0.0;
        }
        return static_cast<double>(llc_load_misses) / static_cast<double>(llc_loads);
    }

    /**
     *  Returns the counts accumulated since an earlier reading of the same group.
     *
     *  @param      earlier  A reading taken before this one.
     *  @return     The per-counter difference.
     */
    [[nodiscard]] constexpr auto since(const PmuGroupReading& earlier) const noexcept
        -> PmuGroupReading
    {
        return PmuGroupReading{
            .cycles = cycles - earlier.cycles,
            .instructions = instructions - earlier.instructions,
            .llc_loads = llc_loads - earlier.llc_loads,
            .llc_load_misses = llc_load_misses - earlier.llc_load_misses,
            .branch_misses = branch_misses - earlier.branch_misses,
        };
    }
};

/**
 *  Wrapper for a group of hardware performance counters.
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
     *  File descriptors of one group, in counter order.
     */
    using GroupFds = std::array<int, kCounterCount>;

    /**
     *  One group per core PMU. Unused groups hold kInvalidFd.
     */
    using GroupFdArray = std::array<GroupFds, kMaxCorePmus>;

    /**
     *  Private constructor
     *
     *  @param      groups  The perf_event file descriptors of each group.
     */
    explicit PmuGroup(GroupFdArray groups) noexcept;

    /**
     *  Returns a group array with every file descriptor invalid.
     */
    [[nodiscard]] static auto invalidGroups() noexcept -> GroupFdArray;

    /**
     *  Applies a perf_event ioctl to every counter through the group leaders.
     *
     *  @param      request  The PERF_EVENT_IOC_* request.
     *  @return     Success or PmuError on failure.
     */
    [[nodiscard]] auto ioctlAll(unsigned long request) const -> std::expected<void, core::PmuError>;

    /**
     *  Closes all valid file descriptors.
     */
    void closeAll() noexcept;

    static constexpr int kInvalidFd = -1;

    GroupFdArray groups_;
};

}  // namespace threveal::collection

#endif  // THREVEAL_COLLECTION_PMU_GROUP_HPP_

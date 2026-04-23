/**
 *  @file       pmu_counter.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Wrapper for Linux perf_event hardware performance counters.
 */

#ifndef THREVEAL_COLLECTION_PMU_COUNTER_HPP_
#define THREVEAL_COLLECTION_PMU_COUNTER_HPP_

#include "threveal/collection/perf_event.hpp"
#include "threveal/core/errors.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <sys/types.h>

namespace threveal::collection
{

/**
 *  Wrapper for a single hardware performance counter.
 */
class PmuCounter
{
  public:
    /**
     *  Creates a new PMU counter for the specified event and target.
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
     *  Returns the file descriptor of the first core PMU.
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
     *  One perf_event file descriptor per core PMU. Unused slots hold kInvalidFd.
     */
    using FdArray = std::array<int, kMaxCorePmus>;

    /**
     *  Private constructor - use create() factory method.
     *
     *  @param      fds    The perf_event file descriptors, first slot always valid.
     *  @param      event  The event type being counted.
     */
    PmuCounter(FdArray fds, PmuEventType event) noexcept;

    /**
     *  Applies a perf_event ioctl to every open file descriptor.
     *
     *  @param      request  The PERF_EVENT_IOC_* request.
     *  @return     Success or PmuError on failure.
     */
    [[nodiscard]] auto ioctlAll(unsigned long request) const -> std::expected<void, core::PmuError>;

    /**
     *  Closes every open file descriptor.
     */
    void closeAll() noexcept;

    static constexpr int kInvalidFd = -1;

    FdArray fds_;
    PmuEventType event_type_;
};

}  // namespace threveal::collection

#endif  // THREVEAL_COLLECTION_PMU_COUNTER_HPP_

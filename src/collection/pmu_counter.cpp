/**
 *  @file       pmu_counter.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Implementation of the PmuCounter class using Linux perf_event_open().
 */

#include "threveal/collection/pmu_counter.hpp"

#include "threveal/collection/perf_event.hpp"
#include "threveal/core/errors.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <expected>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

namespace threveal::collection
{

namespace
{

/**
 *  Configures a perf_event_attr structure for a hardware event.
 *
 *  @param      config  The PERF_COUNT_HW_* constant for the desired event.
 *  @return     Configured perf_event_attr structure ready for perf_event_open().
 */
auto makeHardwareEventAttr(std::uint64_t config) -> perf_event_attr
{
    perf_event_attr attr{};

    // Zero-initialize to ensure all fields have defined values.
    // perf_event_attr has many optional fields that must be zero if unused.
    std::memset(&attr, 0, sizeof(attr));

    attr.type = PERF_TYPE_HARDWARE;

    // Required for kernel version compatibility
    attr.size = sizeof(attr);

    // The specific hardware event (cycles, instructions, etc.)
    attr.config = config;

    // Start disabled so caller can set up multiple counters before enabling
    attr.disabled = 1;

    // Exclude kernel and hypervisor to avoid needing elevated privileges.
    // This means we only count events that occur in user-space.
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;

    return attr;
}

/**
 *  Configures a perf_event_attr structure for a cache event.
 *
 *  @param      cache_id   The cache level (e.g., PERF_COUNT_HW_CACHE_LL).
 *  @param      op_id      The operation (e.g., PERF_COUNT_HW_CACHE_OP_READ).
 *  @param      result_id  The result type (e.g., PERF_COUNT_HW_CACHE_RESULT_MISS).
 *  @return     Configured perf_event_attr structure ready for perf_event_open().
 */
auto makeCacheEventAttr(std::uint64_t cache_id, std::uint64_t op_id, std::uint64_t result_id)
    -> perf_event_attr
{
    perf_event_attr attr{};
    std::memset(&attr, 0, sizeof(attr));

    attr.type = PERF_TYPE_HW_CACHE;
    attr.size = sizeof(attr);

    // Encode cache_id, operation, and result into the config field.
    attr.config = cache_id | (op_id << 8) | (result_id << 16);

    attr.disabled = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;

    return attr;
}

/**
 *  Creates a perf_event_attr for the given PmuEventType.
 *
 *  @param      event  The PMU event type to configure.
 *  @return     Configured perf_event_attr structure for the requested event.
 */
auto makeEventAttr(PmuEventType event) -> perf_event_attr
{
    switch (event)
    {
        case PmuEventType::kCycles:

            // Total CPU cycles elapsed
            return makeHardwareEventAttr(PERF_COUNT_HW_CPU_CYCLES);

        case PmuEventType::kInstructions:

            // Retired instructions
            return makeHardwareEventAttr(PERF_COUNT_HW_INSTRUCTIONS);

        case PmuEventType::kBranchMisses:

            // Branch predictions that were incorrect
            return makeHardwareEventAttr(PERF_COUNT_HW_BRANCH_MISSES);

        case PmuEventType::kLlcLoads:

            // Last-level cache read accesses (hits + misses)
            return makeCacheEventAttr(PERF_COUNT_HW_CACHE_LL, PERF_COUNT_HW_CACHE_OP_READ,
                                      PERF_COUNT_HW_CACHE_RESULT_ACCESS);

        case PmuEventType::kLlcLoadMisses:

            // Last-level cache read misses
            return makeCacheEventAttr(PERF_COUNT_HW_CACHE_LL, PERF_COUNT_HW_CACHE_OP_READ,
                                      PERF_COUNT_HW_CACHE_RESULT_MISS);
    }

    // Unreachable if all enum cases handled, but provides safe fallback
    return makeHardwareEventAttr(PERF_COUNT_HW_CPU_CYCLES);
}

}  // namespace

PmuCounter::PmuCounter(int fd, PmuEventType event) noexcept : fd_(fd), event_type_(event) {}

PmuCounter::~PmuCounter()
{
    // Close the perf_event file descriptor to release the PMU resource
    if (fd_ != kInvalidFd)
    {
        close(fd_);
    }
}

PmuCounter::PmuCounter(PmuCounter&& other) noexcept
    : fd_(std::exchange(other.fd_, kInvalidFd)), event_type_(other.event_type_)
{
    // std::exchange atomically takes ownership and invalidates the source
}

auto PmuCounter::operator=(PmuCounter&& other) noexcept -> PmuCounter&
{
    if (this != &other)
    {
        // Close our existing fd before taking ownership of other's
        if (fd_ != kInvalidFd)
        {
            close(fd_);
        }

        // Transfer ownership and invalidate source
        fd_ = std::exchange(other.fd_, kInvalidFd);
        event_type_ = other.event_type_;
    }
    return *this;
}

auto PmuCounter::create(PmuEventType event, pid_t tid, int cpu)
    -> std::expected<PmuCounter, core::PmuError>
{
    auto attr = makeEventAttr(event);

    // Open the perf_event file descriptor
    pid_t effective_tid = (tid == -1) ? 0 : tid;

    int fd = perfEventOpen(&attr, effective_tid, cpu, -1, 0);

    if (fd < 0)
    {
        // perf_event_open failed, convert errno to our error type
        return std::unexpected(errnoToPmuError(errno));
    }

    return PmuCounter{fd, event};
}

auto PmuCounter::read() const -> std::expected<std::uint64_t, core::PmuError>
{
    if (fd_ == kInvalidFd)
    {
        return std::unexpected(core::PmuError::kInvalidState);
    }

    // Reading from a perf_event fd returns the accumulated counter value.
    std::uint64_t value = 0;
    ssize_t bytes_read = ::read(fd_, &value, sizeof(value));

    if (bytes_read != sizeof(value))
    {
        // Partial read or error - counter may have been closed
        return std::unexpected(core::PmuError::kReadFailed);
    }

    return value;
}

auto PmuCounter::reset() const -> std::expected<void, core::PmuError>
{
    if (fd_ == kInvalidFd)
    {
        return std::unexpected(core::PmuError::kInvalidState);
    }

    // PERF_EVENT_IOC_RESET zeros the counter value.
    // The counter continues in its current enabled/disabled state.
    if (ioctl(fd_, PERF_EVENT_IOC_RESET, 0) < 0)
    {
        return std::unexpected(core::PmuError::kInvalidState);
    }

    return {};
}

auto PmuCounter::enable() const -> std::expected<void, core::PmuError>
{
    if (fd_ == kInvalidFd)
    {
        return std::unexpected(core::PmuError::kInvalidState);
    }

    // PERF_EVENT_IOC_ENABLE starts the counter.
    // Events are accumulated from this point until disable() is called.
    if (ioctl(fd_, PERF_EVENT_IOC_ENABLE, 0) < 0)
    {
        return std::unexpected(core::PmuError::kInvalidState);
    }

    return {};
}

auto PmuCounter::disable() const -> std::expected<void, core::PmuError>
{
    if (fd_ == kInvalidFd)
    {
        return std::unexpected(core::PmuError::kInvalidState);
    }

    // PERF_EVENT_IOC_DISABLE stops counting but preserves the current value.
    // The counter can be read after disabling to get the final count.
    if (ioctl(fd_, PERF_EVENT_IOC_DISABLE, 0) < 0)
    {
        return std::unexpected(core::PmuError::kInvalidState);
    }

    return {};
}

auto PmuCounter::eventType() const noexcept -> PmuEventType
{
    return event_type_;
}

auto PmuCounter::fileDescriptor() const noexcept -> int
{
    return fd_;
}

auto PmuCounter::isValid() const noexcept -> bool
{
    return fd_ != kInvalidFd;
}

}  // namespace threveal::collection

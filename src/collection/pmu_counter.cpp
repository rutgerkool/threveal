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
#include <expected>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

namespace threveal::collection
{

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

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
#include <cstddef>
#include <cstdint>
#include <expected>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

namespace threveal::collection
{

PmuCounter::PmuCounter(FdArray fds, PmuEventType event) noexcept : fds_(fds), event_type_(event) {}

PmuCounter::~PmuCounter()
{
    closeAll();
}

PmuCounter::PmuCounter(PmuCounter&& other) noexcept
    : fds_(other.fds_), event_type_(other.event_type_)
{
    other.fds_.fill(kInvalidFd);
}

auto PmuCounter::operator=(PmuCounter&& other) noexcept -> PmuCounter&
{
    if (this != &other)
    {
        closeAll();
        fds_ = other.fds_;
        event_type_ = other.event_type_;
        other.fds_.fill(kInvalidFd);
    }
    return *this;
}

void PmuCounter::closeAll() noexcept
{
    for (int& fd : fds_)
    {
        if (fd != kInvalidFd)
        {
            close(fd);
            fd = kInvalidFd;
        }
    }
}

auto PmuCounter::create(PmuEventType event, pid_t tid, int cpu)
    -> std::expected<PmuCounter, core::PmuError>
{
    pid_t effective_tid = (tid == -1) ? 0 : tid;
    auto pmu_types = corePmuTypesFor(cpu);

    FdArray fds{};
    fds.fill(kInvalidFd);

    PmuCounter counter{fds, event};

    for (std::size_t i = 0; i < pmu_types.size(); ++i)
    {
        auto attr = makeEventAttr(event, pmu_types[i]);

        counter.fds_.at(i) = perfEventOpen(&attr, effective_tid, cpu, -1, 0);
        if (counter.fds_.at(i) < 0)
        {
            return std::unexpected(errnoToPmuError(errno));
        }
    }

    return counter;
}

auto PmuCounter::read() const -> std::expected<std::uint64_t, core::PmuError>
{
    if (!isValid())
    {
        return std::unexpected(core::PmuError::kInvalidState);
    }

    // Each core PMU only counts while the thread runs on its core type
    std::uint64_t total = 0;
    for (int fd : fds_)
    {
        if (fd == kInvalidFd)
        {
            continue;
        }

        std::uint64_t value = 0;
        if (::read(fd, &value, sizeof(value)) != sizeof(value))
        {
            return std::unexpected(core::PmuError::kReadFailed);
        }
        total += value;
    }

    return total;
}

auto PmuCounter::reset() const -> std::expected<void, core::PmuError>
{
    return ioctlAll(PERF_EVENT_IOC_RESET);
}

auto PmuCounter::enable() const -> std::expected<void, core::PmuError>
{
    return ioctlAll(PERF_EVENT_IOC_ENABLE);
}

auto PmuCounter::disable() const -> std::expected<void, core::PmuError>
{
    // Stops counting but keeps the value readable
    return ioctlAll(PERF_EVENT_IOC_DISABLE);
}

auto PmuCounter::ioctlAll(unsigned long request) const -> std::expected<void, core::PmuError>
{
    if (!isValid())
    {
        return std::unexpected(core::PmuError::kInvalidState);
    }

    for (int fd : fds_)
    {
        if (fd != kInvalidFd && ioctl(fd, request, 0) < 0)
        {
            return std::unexpected(core::PmuError::kInvalidState);
        }
    }

    return {};
}

auto PmuCounter::eventType() const noexcept -> PmuEventType
{
    return event_type_;
}

auto PmuCounter::fileDescriptor() const noexcept -> int
{
    return fds_[0];
}

auto PmuCounter::isValid() const noexcept -> bool
{
    return fds_[0] != kInvalidFd;
}

}  // namespace threveal::collection

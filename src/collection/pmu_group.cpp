/**
 *  @file       pmu_group.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Implementation of the PmuGroup class using Linux perf_event groups.
 */

#include "threveal/collection/pmu_group.hpp"

#include "threveal/collection/perf_event.hpp"
#include "threveal/core/errors.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <expected>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

namespace threveal::collection
{

namespace
{

/**
 *  Index constants for the counter array.
 */
enum CounterIndex : std::uint8_t
{
    kCycles = 0,         // Group leader, must be first
    kInstructions = 1,   // For IPC calculation
    kLlcLoads = 2,       // Cache miss rate denominator
    kLlcLoadMisses = 3,  // Indicates cache state destruction
    kBranchMisses = 4,   // May spike after migration
};

/**
 *  Events in counter order, matching CounterIndex. The leader must come first.
 */
constexpr std::array<PmuEventType, PmuGroup::kCounterCount> kGroupEvents{
    PmuEventType::kCycles,        PmuEventType::kInstructions, PmuEventType::kLlcLoads,
    PmuEventType::kLlcLoadMisses, PmuEventType::kBranchMisses,
};

/**
 *  Creates the attributes for the group leader, which reads all members at once.
 */
auto makeLeaderAttr(PmuEventType event) -> perf_event_attr
{
    auto attr = makeEventAttr(event);
    attr.read_format = PERF_FORMAT_GROUP;
    return attr;
}

/**
 *  Creates the attributes for a group member, which follows the leader's enable state.
 */
auto makeMemberAttr(PmuEventType event) -> perf_event_attr
{
    auto attr = makeEventAttr(event);
    attr.disabled = 0;
    return attr;
}

/**
 *  Structure for reading group format data.
 */
struct GroupReadFormat
{
    std::uint64_t nr;                                           // Number of counters in group
    std::array<std::uint64_t, PmuGroup::kCounterCount> values;  // Counter values in order
};

}  // namespace

PmuGroup::PmuGroup(std::array<int, kCounterCount> fds) noexcept : fds_(fds) {}

PmuGroup::~PmuGroup()
{
    closeAll();
}

PmuGroup::PmuGroup(PmuGroup&& other) noexcept : fds_(other.fds_)
{
    // Invalidate source to prevent double-close
    other.fds_.fill(kInvalidFd);
}

auto PmuGroup::operator=(PmuGroup&& other) noexcept -> PmuGroup&
{
    if (this != &other)
    {
        // Release our current resources first
        closeAll();

        fds_ = other.fds_;

        // Invalidate source
        other.fds_.fill(kInvalidFd);
    }
    return *this;
}

void PmuGroup::closeAll() noexcept
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

auto PmuGroup::create(pid_t tid, int cpu) -> std::expected<PmuGroup, core::PmuError>
{
    std::array<int, kCounterCount> fds{};
    fds.fill(kInvalidFd);

    // Owning the fds up front lets the destructor close them if a later open fails
    PmuGroup group{fds};

    for (std::size_t i = 0; i < kCounterCount; ++i)
    {
        bool is_leader = (i == 0);
        auto event = kGroupEvents.at(i);
        auto attr = is_leader ? makeLeaderAttr(event) : makeMemberAttr(event);
        int group_fd = is_leader ? -1 : group.fds_[kCycles];

        group.fds_.at(i) = perfEventOpen(&attr, tid, cpu, group_fd, 0);
        if (group.fds_.at(i) < 0)
        {
            return std::unexpected(errnoToPmuError(errno));
        }
    }

    return group;
}

auto PmuGroup::read() const -> std::expected<PmuGroupReading, core::PmuError>
{
    if (!isValid())
    {
        return std::unexpected(core::PmuError::kInvalidState);
    }

    GroupReadFormat data{};

    ssize_t bytes_read = ::read(fds_[kCycles], &data, sizeof(data));

    if (bytes_read < 0)
    {
        return std::unexpected(core::PmuError::kReadFailed);
    }

    // Ensure we got enough bytes
    if (static_cast<std::size_t>(bytes_read) < sizeof(data.nr))
    {
        return std::unexpected(core::PmuError::kReadFailed);
    }

    // Verify counter count matches
    if (data.nr != kCounterCount)
    {
        return std::unexpected(core::PmuError::kReadFailed);
    }

    return PmuGroupReading{
        .cycles = data.values[kCycles],
        .instructions = data.values[kInstructions],
        .llc_loads = data.values[kLlcLoads],
        .llc_load_misses = data.values[kLlcLoadMisses],
        .branch_misses = data.values[kBranchMisses],
    };
}

auto PmuGroup::reset() const -> std::expected<void, core::PmuError>
{
    if (!isValid())
    {
        return std::unexpected(core::PmuError::kInvalidState);
    }

    // FLAG_GROUP resets all members atomically
    if (ioctl(fds_[kCycles], PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) < 0)
    {
        return std::unexpected(core::PmuError::kInvalidState);
    }

    return {};
}

auto PmuGroup::enable() const -> std::expected<void, core::PmuError>
{
    if (!isValid())
    {
        return std::unexpected(core::PmuError::kInvalidState);
    }

    // FLAG_GROUP enables all members simultaneously
    if (ioctl(fds_[kCycles], PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) < 0)
    {
        return std::unexpected(core::PmuError::kInvalidState);
    }

    return {};
}

auto PmuGroup::disable() const -> std::expected<void, core::PmuError>
{
    if (!isValid())
    {
        return std::unexpected(core::PmuError::kInvalidState);
    }

    // FLAG_GROUP disables all members
    if (ioctl(fds_[kCycles], PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) < 0)
    {
        return std::unexpected(core::PmuError::kInvalidState);
    }

    return {};
}

auto PmuGroup::isValid() const noexcept -> bool
{
    // Valid only if ALL file descriptors are valid
    return std::ranges::all_of(fds_,
                               [](int fd)
                               {
                                   return fd != kInvalidFd;
                               });
}

}  // namespace threveal::collection

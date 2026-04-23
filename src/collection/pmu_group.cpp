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
#include <cstddef>
#include <cstdint>
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
auto makeLeaderAttr(PmuEventType event, std::uint32_t pmu_type) -> perf_event_attr
{
    auto attr = makeEventAttr(event, pmu_type);
    attr.read_format = PERF_FORMAT_GROUP;
    return attr;
}

/**
 *  Creates the attributes for a group member, which follows the leader's enable state.
 */
auto makeMemberAttr(PmuEventType event, std::uint32_t pmu_type) -> perf_event_attr
{
    auto attr = makeEventAttr(event, pmu_type);
    attr.disabled = 0;
    return attr;
}

/**
 *  Opens one complete counter group on a single PMU.
 *
 *  @param      fds       Receives the file descriptors, in counter order.
 *  @param      tid       Thread ID to monitor.
 *  @param      cpu       CPU to monitor, or -1 for any CPU.
 *  @param      pmu_type  The PMU that should count the events.
 *  @return     Success or PmuError on failure.
 */
auto openGroup(std::array<int, PmuGroup::kCounterCount>& fds, pid_t tid, int cpu,
               std::uint32_t pmu_type) -> std::expected<void, core::PmuError>
{
    for (std::size_t i = 0; i < fds.size(); ++i)
    {
        bool is_leader = (i == 0);
        auto event = kGroupEvents.at(i);
        auto attr = is_leader ? makeLeaderAttr(event, pmu_type) : makeMemberAttr(event, pmu_type);
        int group_fd = is_leader ? -1 : fds[kCycles];

        fds.at(i) = perfEventOpen(&attr, tid, cpu, group_fd, 0);
        if (fds.at(i) < 0)
        {
            return std::unexpected(errnoToPmuError(errno));
        }
    }

    return {};
}

/**
 *  Structure for reading group format data.
 */
struct GroupReadFormat
{
    std::uint64_t nr;                                           // Number of counters in group
    std::array<std::uint64_t, PmuGroup::kCounterCount> values;  // Counter values in order
};

/**
 *  Reads every counter of one group through its leader.
 *
 *  @param      leader_fd  File descriptor of the group leader.
 *  @return     The counter values in counter order, or PmuError on failure.
 */
auto readGroupValues(int leader_fd)
    -> std::expected<std::array<std::uint64_t, PmuGroup::kCounterCount>, core::PmuError>
{
    GroupReadFormat data{};

    ssize_t bytes_read = ::read(leader_fd, &data, sizeof(data));
    if (bytes_read < 0 || static_cast<std::size_t>(bytes_read) < sizeof(data.nr))
    {
        return std::unexpected(core::PmuError::kReadFailed);
    }

    if (data.nr != PmuGroup::kCounterCount)
    {
        return std::unexpected(core::PmuError::kReadFailed);
    }

    return data.values;
}

}  // namespace

PmuGroup::PmuGroup(GroupFdArray groups) noexcept : groups_(groups) {}

PmuGroup::~PmuGroup()
{
    closeAll();
}

PmuGroup::PmuGroup(PmuGroup&& other) noexcept
    : groups_(std::exchange(other.groups_, invalidGroups()))
{
}

auto PmuGroup::operator=(PmuGroup&& other) noexcept -> PmuGroup&
{
    if (this != &other)
    {
        closeAll();
        groups_ = std::exchange(other.groups_, invalidGroups());
    }
    return *this;
}

auto PmuGroup::invalidGroups() noexcept -> GroupFdArray
{
    GroupFdArray groups{};
    for (auto& fds : groups)
    {
        fds.fill(kInvalidFd);
    }
    return groups;
}

void PmuGroup::closeAll() noexcept
{
    for (auto& fds : groups_)
    {
        for (int& fd : fds)
        {
            if (fd != kInvalidFd)
            {
                close(fd);
                fd = kInvalidFd;
            }
        }
    }
}

auto PmuGroup::create(pid_t tid, int cpu) -> std::expected<PmuGroup, core::PmuError>
{
    auto pmu_types = corePmuTypesFor(cpu);

    // Owning the fds up front lets the destructor close them if a later open fails
    PmuGroup group{invalidGroups()};

    for (std::size_t i = 0; i < pmu_types.size(); ++i)
    {
        auto opened = openGroup(group.groups_.at(i), tid, cpu, pmu_types[i]);
        if (!opened)
        {
            return std::unexpected(opened.error());
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

    // Each group only counts while the thread runs on its core type
    std::array<std::uint64_t, kCounterCount> totals{};
    for (const auto& fds : groups_)
    {
        if (fds[kCycles] == kInvalidFd)
        {
            continue;
        }

        auto values = readGroupValues(fds[kCycles]);
        if (!values)
        {
            return std::unexpected(values.error());
        }

        for (std::size_t i = 0; i < kCounterCount; ++i)
        {
            totals.at(i) += values->at(i);
        }
    }

    return PmuGroupReading{
        .cycles = totals[kCycles],
        .instructions = totals[kInstructions],
        .llc_loads = totals[kLlcLoads],
        .llc_load_misses = totals[kLlcLoadMisses],
        .branch_misses = totals[kBranchMisses],
    };
}

auto PmuGroup::reset() const -> std::expected<void, core::PmuError>
{
    return ioctlAll(PERF_EVENT_IOC_RESET);
}

auto PmuGroup::enable() const -> std::expected<void, core::PmuError>
{
    return ioctlAll(PERF_EVENT_IOC_ENABLE);
}

auto PmuGroup::disable() const -> std::expected<void, core::PmuError>
{
    return ioctlAll(PERF_EVENT_IOC_DISABLE);
}

auto PmuGroup::ioctlAll(unsigned long request) const -> std::expected<void, core::PmuError>
{
    if (!isValid())
    {
        return std::unexpected(core::PmuError::kInvalidState);
    }

    // FLAG_GROUP applies the request to every member of the leader's group
    for (const auto& fds : groups_)
    {
        if (fds[kCycles] != kInvalidFd && ioctl(fds[kCycles], request, PERF_IOC_FLAG_GROUP) < 0)
        {
            return std::unexpected(core::PmuError::kInvalidState);
        }
    }

    return {};
}

auto PmuGroup::isValid() const noexcept -> bool
{
    return std::ranges::all_of(groups_[0],
                               [](int fd)
                               {
                                   return fd != kInvalidFd;
                               });
}

}  // namespace threveal::collection

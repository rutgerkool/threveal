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
    // Release all PMU resources
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

        // Take ownership
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

    // Cleanup helper to avoid leaking fds on partial failure
    auto cleanup = [&fds]()
    {
        for (int fd : fds)
        {
            if (fd != kInvalidFd)
            {
                close(fd);
            }
        }
    };

    // Create leader first (group_fd=-1 creates new group)
    auto cycles_attr = makeLeaderAttr(PmuEventType::kCycles);
    fds[kCycles] = perfEventOpen(&cycles_attr, tid, cpu, -1, 0);

    if (fds[kCycles] < 0)
    {
        return std::unexpected(errnoToPmuError(errno));
    }

    // All members join the group via leader_fd
    int leader_fd = fds[kCycles];

    // Instructions counter for IPC
    auto instr_attr = makeMemberAttr(PmuEventType::kInstructions);
    fds[kInstructions] = perfEventOpen(&instr_attr, tid, cpu, leader_fd, 0);

    if (fds[kInstructions] < 0)
    {
        auto err = errnoToPmuError(errno);
        cleanup();
        return std::unexpected(err);
    }

    // LLC loads (accesses, i.e. hits + misses)
    auto llc_loads_attr = makeMemberAttr(PmuEventType::kLlcLoads);
    fds[kLlcLoads] = perfEventOpen(&llc_loads_attr, tid, cpu, leader_fd, 0);

    if (fds[kLlcLoads] < 0)
    {
        auto err = errnoToPmuError(errno);
        cleanup();
        return std::unexpected(err);
    }

    // LLC misses (went to memory)
    auto llc_misses_attr = makeMemberAttr(PmuEventType::kLlcLoadMisses);
    fds[kLlcLoadMisses] = perfEventOpen(&llc_misses_attr, tid, cpu, leader_fd, 0);

    if (fds[kLlcLoadMisses] < 0)
    {
        auto err = errnoToPmuError(errno);
        cleanup();
        return std::unexpected(err);
    }

    // Branch mispredictions
    auto branch_attr = makeMemberAttr(PmuEventType::kBranchMisses);
    fds[kBranchMisses] = perfEventOpen(&branch_attr, tid, cpu, leader_fd, 0);

    if (fds[kBranchMisses] < 0)
    {
        auto err = errnoToPmuError(errno);
        cleanup();
        return std::unexpected(err);
    }

    // All counters created; PmuGroup takes ownership
    return PmuGroup{fds};
}

auto PmuGroup::read() const -> std::expected<PmuGroupReading, core::PmuError>
{
    if (!isValid())
    {
        return std::unexpected(core::PmuError::kInvalidState);
    }

    GroupReadFormat data{};

    // Read from leader gets all values atomically
    ssize_t bytes_read = ::read(fds_[kCycles], &data, sizeof(data));

    // Check for read failure
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

    // Map values to struct (order matches CounterIndex enum)
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

    // FLAG_GROUP disables all members; values preserved for reading
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

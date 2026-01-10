/**
 *  @file       pmu_group.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Implementation of the PmuGroup class using Linux perf_event groups.
 */

#include "threveal/collection/pmu_group.hpp"

#include "threveal/core/errors.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <expected>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

namespace threveal::collection
{

namespace
{

/**
 *  Wrapper for the perf_event_open syscall.
 */
auto perfEventOpen(perf_event_attr* attr, pid_t pid, int cpu, int group_fd, unsigned long flags)
    -> int
{
    // glibc doesn't provide a wrapper, so we call the syscall directly
    return static_cast<int>(syscall(SYS_perf_event_open, attr, pid, cpu, group_fd, flags));
}

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
 *  Creates a perf_event_attr for hardware events.
 */
auto makeHardwareAttr(std::uint64_t config, bool is_leader) -> perf_event_attr
{
    perf_event_attr attr{};

    // Zero-init required; perf_event_attr has many optional fields
    std::memset(&attr, 0, sizeof(attr));

    attr.type = PERF_TYPE_HARDWARE;
    attr.size = sizeof(attr);
    attr.config = config;

    // Only leader starts disabled
    attr.disabled = is_leader ? 1 : 0;

    // Exclude kernel/hypervisor to avoid needing CAP_SYS_ADMIN
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;

    // Leader needs GROUP format for atomic multi-counter reads
    if (is_leader)
    {
        attr.read_format = PERF_FORMAT_GROUP;
    }

    return attr;
}

/**
 *  Creates a perf_event_attr for cache events.
 */
auto makeCacheAttr(std::uint64_t cache_id, std::uint64_t op_id, std::uint64_t result_id)
    -> perf_event_attr
{
    perf_event_attr attr{};
    std::memset(&attr, 0, sizeof(attr));

    attr.type = PERF_TYPE_HW_CACHE;
    attr.size = sizeof(attr);

    // Cache events encode three fields: cache level, operation, result
    attr.config = cache_id | (op_id << 8) | (result_id << 16);

    // Members inherit enabled/disabled state from leader
    attr.disabled = 0;

    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;

    return attr;
}

/**
 *  Maps errno values from perf_event_open() to PmuError.
 */
auto errnoToPmuError(int err) -> core::PmuError
{
    switch (err)
    {
        case EACCES:
        case EPERM:
            // Need CAP_PERFMON or perf_event_paranoid <= 1
            return core::PmuError::kPermissionDenied;

        case ENOENT:
        case ENODEV:
        case EOPNOTSUPP:
            // Event not available on this CPU/kernel
            return core::PmuError::kEventNotSupported;

        case ESRCH:
        case EINVAL:
            // Invalid PID or parameter combination
            return core::PmuError::kInvalidTarget;

        case EMFILE:
        case ENFILE:
            // Too many fds or hardware counters exhausted
            return core::PmuError::kTooManyEvents;

        default:
            return core::PmuError::kOpenFailed;
    }
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
    auto cycles_attr = makeHardwareAttr(PERF_COUNT_HW_CPU_CYCLES, true);
    fds[kCycles] = perfEventOpen(&cycles_attr, tid, cpu, -1, 0);

    if (fds[kCycles] < 0)
    {
        return std::unexpected(errnoToPmuError(errno));
    }

    // All members join the group via leader_fd
    int leader_fd = fds[kCycles];

    // Instructions counter for IPC
    auto instr_attr = makeHardwareAttr(PERF_COUNT_HW_INSTRUCTIONS, false);
    fds[kInstructions] = perfEventOpen(&instr_attr, tid, cpu, leader_fd, 0);

    if (fds[kInstructions] < 0)
    {
        auto err = errnoToPmuError(errno);
        cleanup();
        return std::unexpected(err);
    }

    // LLC loads (accesses, i.e. hits + misses)
    auto llc_loads_attr = makeCacheAttr(PERF_COUNT_HW_CACHE_LL, PERF_COUNT_HW_CACHE_OP_READ,
                                        PERF_COUNT_HW_CACHE_RESULT_ACCESS);
    fds[kLlcLoads] = perfEventOpen(&llc_loads_attr, tid, cpu, leader_fd, 0);

    if (fds[kLlcLoads] < 0)
    {
        auto err = errnoToPmuError(errno);
        cleanup();
        return std::unexpected(err);
    }

    // LLC misses (went to memory)
    auto llc_misses_attr = makeCacheAttr(PERF_COUNT_HW_CACHE_LL, PERF_COUNT_HW_CACHE_OP_READ,
                                         PERF_COUNT_HW_CACHE_RESULT_MISS);
    fds[kLlcLoadMisses] = perfEventOpen(&llc_misses_attr, tid, cpu, leader_fd, 0);

    if (fds[kLlcLoadMisses] < 0)
    {
        auto err = errnoToPmuError(errno);
        cleanup();
        return std::unexpected(err);
    }

    // Branch mispredictions
    auto branch_attr = makeHardwareAttr(PERF_COUNT_HW_BRANCH_MISSES, false);
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

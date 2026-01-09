/**
 *  @file       pmu_counter.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Implementation of the PmuCounter class using Linux perf_event_open().
 */

#include "threveal/collection/pmu_counter.hpp"

#include "threveal/core/errors.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <expected>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

namespace threveal::collection
{

namespace
{

/**
 *  Wrapper for the perf_event_open syscall.
 *
 *  glibc does not provide a wrapper for perf_event_open(), so we must invoke
 *  the syscall directly. This is the standard approach used by perf tools.
 *
 *  @param      attr      Pointer to perf_event_attr configuration structure.
 *  @param      pid       Process/thread ID to monitor (-1 for calling thread).
 *  @param      cpu       CPU to monitor (-1 for any CPU the thread runs on).
 *  @param      group_fd  File descriptor of group leader (-1 for new group).
 *  @param      flags     Additional flags (usually 0).
 *  @return     File descriptor on success, -1 on error with errno set.
 */
auto perfEventOpen(perf_event_attr* attr, pid_t pid, int cpu, int group_fd, unsigned long flags)
    -> int
{
    return static_cast<int>(syscall(SYS_perf_event_open, attr, pid, cpu, group_fd, flags));
}

/**
 *  Configures a perf_event_attr structure for a hardware event.
 *
 *  Creates an attribute structure with common settings:
 *  - disabled=1: Counter starts disabled, must call enable() explicitly
 *  - exclude_kernel=1: Only count user-space events (avoids CAP_SYS_ADMIN)
 *  - exclude_hv=1: Exclude hypervisor events
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
    attr.size = sizeof(attr);  // Required for kernel version compatibility
    attr.config = config;      // The specific hardware event (cycles, instructions, etc.)

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
 *  Cache events use a composite config value encoding three fields:
 *  - bits 0-7:   cache ID (L1D, L1I, LL, DTLB, ITLB, BPU, NODE)
 *  - bits 8-15:  operation (READ, WRITE, PREFETCH)
 *  - bits 16-23: result (ACCESS, MISS)
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
    // Example: LLC read misses = LL | (READ << 8) | (MISS << 16)
    attr.config = cache_id | (op_id << 8) | (result_id << 16);

    attr.disabled = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;

    return attr;
}

/**
 *  Creates a perf_event_attr for the given PmuEventType.
 *
 *  Maps our PmuEventType enum to the appropriate perf_event configuration.
 *  Hardware events use PERF_TYPE_HARDWARE, cache events use PERF_TYPE_HW_CACHE.
 *
 *  @param      event  The PMU event type to configure.
 *  @return     Configured perf_event_attr structure for the requested event.
 */
auto makeEventAttr(PmuEventType event) -> perf_event_attr
{
    switch (event)
    {
        case PmuEventType::kCycles:
            // Total CPU cycles elapsed (affected by frequency scaling)
            return makeHardwareEventAttr(PERF_COUNT_HW_CPU_CYCLES);

        case PmuEventType::kInstructions:
            // Retired instructions (completed, not speculative)
            return makeHardwareEventAttr(PERF_COUNT_HW_INSTRUCTIONS);

        case PmuEventType::kBranchMisses:
            // Branch predictions that were incorrect
            return makeHardwareEventAttr(PERF_COUNT_HW_BRANCH_MISSES);

        case PmuEventType::kLlcLoads:
            // Last-level cache read accesses (hits + misses)
            return makeCacheEventAttr(PERF_COUNT_HW_CACHE_LL, PERF_COUNT_HW_CACHE_OP_READ,
                                      PERF_COUNT_HW_CACHE_RESULT_ACCESS);

        case PmuEventType::kLlcLoadMisses:
            // Last-level cache read misses (went to memory)
            return makeCacheEventAttr(PERF_COUNT_HW_CACHE_LL, PERF_COUNT_HW_CACHE_OP_READ,
                                      PERF_COUNT_HW_CACHE_RESULT_MISS);
    }

    // Unreachable if all enum cases handled, but provides safe fallback
    return makeHardwareEventAttr(PERF_COUNT_HW_CPU_CYCLES);
}

/**
 *  Maps errno values from perf_event_open() to PmuError.
 *
 *  perf_event_open() can fail with various errno values depending on the
 *  specific failure condition. This function translates them to our typed
 *  error enum for consistent error handling.
 *
 *  @param      err  The errno value to translate.
 *  @return     The corresponding PmuError value.
 */
auto errnoToPmuError(int err) -> core::PmuError
{
    switch (err)
    {
        case EACCES:
        case EPERM:
            // User lacks CAP_PERFMON capability or perf_event_paranoid is too high.
            // Fix: run as root, grant CAP_PERFMON, or set perf_event_paranoid <= 1
            return core::PmuError::kPermissionDenied;

        case ENOENT:
        case ENODEV:
        case EOPNOTSUPP:
            // The requested event is not available on this CPU or kernel.
            // This can happen with cache events on some microarchitectures.
            return core::PmuError::kEventNotSupported;

        case ESRCH:
        case EINVAL:
            // Invalid PID/TID specified, or invalid combination of parameters
            return core::PmuError::kInvalidTarget;

        case EMFILE:
        case ENFILE:
            // Too many open file descriptors or PMU hardware counters exhausted.
            // Most CPUs only have 4-8 programmable counters.
            return core::PmuError::kTooManyEvents;

        default:
            return core::PmuError::kOpenFailed;
    }
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

    // Open the perf_event file descriptor:
    // - tid=0: monitor the calling thread (note: -1 means "all processes" which
    //          requires cpu >= 0, so we use 0 for "self")
    // - cpu=-1: monitor on any CPU the thread runs on
    // - group_fd=-1: not part of an event group (standalone counter)
    // - flags=0: no special flags
    //
    // Special case: if caller passes tid=-1, treat as "self" (tid=0)
    // because tid=-1 with cpu=-1 is invalid per perf_event_open(2)
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
    // The read format depends on attr.read_format; we use the default which
    // returns a single uint64_t count value.
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

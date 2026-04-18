/**
 *  @file       perf_event.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Implementation of the shared perf_event_open() helpers.
 */

#include "threveal/collection/perf_event.hpp"

#include "threveal/core/errors.hpp"

#include <cerrno>
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

namespace threveal::collection
{

auto perfEventOpen(perf_event_attr* attr, pid_t pid, int cpu, int group_fd, unsigned long flags)
    -> int
{
    return static_cast<int>(syscall(SYS_perf_event_open, attr, pid, cpu, group_fd, flags));
}

auto errnoToPmuError(int err) noexcept -> core::PmuError
{
    switch (err)
    {
        case EACCES:
        case EPERM:

            // User lacks CAP_PERFMON capability or perf_event_paranoid is too high.
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
            return core::PmuError::kTooManyEvents;

        default:
            return core::PmuError::kOpenFailed;
    }
}

}  // namespace threveal::collection

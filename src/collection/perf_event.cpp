/**
 *  @file       perf_event.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Implementation of the shared perf_event_open() helpers.
 */

#include "threveal/collection/perf_event.hpp"

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

}  // namespace threveal::collection

/**
 *  @file       bpf_common.h
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Common definitions shared between eBPF programs and userspace.
 *
 *  This header defines data structures that are used by both the eBPF
 *  programs running in kernel space and the C++ code in userspace.
 *  It must be valid C (not C++) for eBPF compilation.
 */

#ifndef THREVEAL_BPF_COMMON_H_
#define THREVEAL_BPF_COMMON_H_

/* Provide types for both BPF and userspace contexts */
#ifdef __BPF__
/* BPF context: types come from vmlinux.h (included before this header) */
#else
/* Userspace context: use Linux types header */
#include <linux/types.h>
#endif

/**
 *  Maximum length of a process/thread command name.
 *
 *  Linux kernel limits comm to 16 bytes including null terminator.
 *  This must match core::kMaxCommLength in events.hpp.
 */
#define MAX_COMM_LEN 16

/**
 *  Migration event captured from sched_migrate_task tracepoint.
 *
 *  This structure is written by the eBPF program and read by userspace
 *  via the ring buffer. Fields are ordered to minimize padding.
 */
struct migration_event
{
    /**
     *  Timestamp when the migration occurred (nanoseconds since boot).
     *  Obtained via bpf_ktime_get_ns().
     */
    __u64 timestamp_ns;

    /**
     *  Process ID of the migrated task.
     */
    __u32 pid;

    /**
     *  Thread ID of the migrated task.
     */
    __u32 tid;

    /**
     *  Source CPU ID (where the task was running before migration).
     */
    __u32 src_cpu;

    /**
     *  Destination CPU ID (where the task is running after migration).
     */
    __u32 dst_cpu;

    /**
     *  Command name of the migrated task (may be truncated).
     */
    char comm[MAX_COMM_LEN];
};

#endif /* THREVEAL_BPF_COMMON_H_ */

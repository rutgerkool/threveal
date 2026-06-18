/**
 *  @file       migration_tracker.bpf.c
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  eBPF program for tracking scheduler migration events.
 *
 *  This program attaches to the sched:sched_migrate_task tracepoint to capture
 *  thread migrations between CPUs. Events are sent to userspace via a ring
 *  buffer for correlation with PMU counter data.
 *
 *  Compilation: clang -g -O2 -target bpf -D__TARGET_ARCH_x86 -c migration_tracker.bpf.c
 */

/* vmlinux.h must be included first - provides all kernel type definitions */
#include "vmlinux.h"

/* BPF helper definitions */
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

/* Shared data structures with userspace (__BPF__ is auto-defined by clang) */
#include "bpf_common.h"

/**
 *  Ring buffer for sending migration events to userspace.
 *
 *  Ring buffers (BPF_MAP_TYPE_RINGBUF) are the preferred mechanism for
 *  high-frequency event streaming since Linux 5.8. They provide:
 *  - Lock-free operation
 *  - Variable-size records
 *  - Automatic memory management
 *
 *  Size: 256KB (262144 bytes) - sufficient for ~5000 events before wrap
 */
struct
{
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} events SEC(".maps");

/**
 *  Optional PID filter for targeted tracing.
 *
 *  When target_pid is non-zero, only migrations for that process (and its
 *  threads) are captured. This reduces overhead when profiling a specific
 *  application.
 *
 *  Set from userspace via BPF map update before attaching the program.
 */
struct
{
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u32);
} migration_config SEC(".maps");

/* Configuration key for target PID */
#define CONFIG_TARGET_PID 0

/**
 *  Layout of task_struct before Linux 5.16, which kept the CPU in task_struct itself.
 */
struct task_struct___pre_5_16
{
    unsigned int cpu;
} __attribute__((preserve_access_index));

/**
 *  Returns the CPU a task is currently assigned to.
 *
 *  @param      task  The task to inspect
 *  @return     The task's CPU
 */
static __always_inline __u32 task_cpu(const struct task_struct *task)
{
    if (bpf_core_field_exists(task->thread_info.cpu))
    {
        return BPF_CORE_READ(task, thread_info.cpu);
    }
    return BPF_CORE_READ((struct task_struct___pre_5_16 *)task, cpu);
}

/**
 *  BTF tracepoint handler for sched:sched_migrate_task.
 *
 *  The tracepoint fires wherever the scheduler moves a task, which is often not
 *  in the context of the moved task itself: a waker migrates the task it wakes,
 *  and load balancing runs in kernel threads. All identity fields are therefore
 *  read from the migrated task, never from the current task.
 *
 *  @param      task      The task being migrated
 *  @param      dest_cpu  The CPU the task is moved to
 *  @return     0 on success (required by BPF verifier)
 */
SEC("tp_btf/sched_migrate_task")
int BPF_PROG(handle_sched_migrate_task, struct task_struct *task, int dest_cpu)
{
    struct migration_event *event;
    __u32 key = CONFIG_TARGET_PID;
    __u32 *target_pid;
    __u32 pid = BPF_CORE_READ(task, tgid);

    /* Check if we should filter by PID */
    target_pid = bpf_map_lookup_elem(&migration_config, &key);
    if (target_pid && *target_pid != 0 && pid != *target_pid)
    {
        return 0;
    }

    /* Reserve space in ring buffer for the event */
    event = bpf_ringbuf_reserve(&events, sizeof(*event), 0);
    if (!event)
    {
        /* Ring buffer full - drop event (userspace not consuming fast enough) */
        return 0;
    }

    event->timestamp_ns = bpf_ktime_get_ns();
    event->pid = pid;
    event->tid = BPF_CORE_READ(task, pid);
    event->src_cpu = task_cpu(task);
    event->dst_cpu = dest_cpu;
    BPF_CORE_READ_STR_INTO(&event->comm, task, comm);

    bpf_ringbuf_submit(event, 0);

    return 0;
}

/**
 *  BPF program license declaration.
 *
 *  Must be GPL compatible to use certain BPF helpers like bpf_get_current_comm.
 *  This is a requirement of the Linux kernel's BPF subsystem.
 */
char LICENSE[] SEC("license") = "GPL";

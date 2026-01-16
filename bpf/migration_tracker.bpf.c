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
 *  Tracepoint handler for sched:sched_migrate_task.
 *
 *  This tracepoint fires when the scheduler moves a task from one CPU to
 *  another. The tracepoint arguments provide:
 *  - p: pointer to the task_struct being migrated
 *  - dest_cpu: the destination CPU ID
 *  - orig_cpu is available in the tracepoint context
 *
 *  @param      ctx  Tracepoint context containing event arguments
 *  @return     0 on success (required by BPF verifier)
 */
SEC("tp/sched/sched_migrate_task")
int handle_sched_migrate_task(struct trace_event_raw_sched_migrate_task *ctx)
{
    struct migration_event *event;
    __u32 key = CONFIG_TARGET_PID;
    __u32 *target_pid;
    __u32 pid;
    __u32 tid;

    /* Get current process and thread IDs */
    __u64 pid_tgid = bpf_get_current_pid_tgid();
    pid = pid_tgid >> 32;        /* Upper 32 bits: TGID (process ID) */
    tid = pid_tgid & 0xFFFFFFFF; /* Lower 32 bits: PID (thread ID) */

    /* Check if we should filter by PID */
    target_pid = bpf_map_lookup_elem(&migration_config, &key);
    if (target_pid && *target_pid != 0)
    {
        /* Filter enabled: only capture events for target process */
        if (pid != *target_pid)
        {
            return 0;
        }
    }

    /* Reserve space in ring buffer for the event */
    event = bpf_ringbuf_reserve(&events, sizeof(*event), 0);
    if (!event)
    {
        /* Ring buffer full - drop event (userspace not consuming fast enough) */
        return 0;
    }

    /* Populate event fields */
    event->timestamp_ns = bpf_ktime_get_ns();
    event->pid = pid;
    event->tid = tid;

    /*
     * Read CPU IDs from tracepoint context.
     * The tracepoint structure provides orig_cpu and dest_cpu fields.
     */
    event->src_cpu = ctx->orig_cpu;
    event->dst_cpu = ctx->dest_cpu;

    /* Read command name (process name, max 16 chars) */
    bpf_get_current_comm(&event->comm, sizeof(event->comm));

    /* Submit event to userspace */
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

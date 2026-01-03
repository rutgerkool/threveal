/**
 *  @file       events.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Event data structures for migration tracking and PMU sampling.
 *
 *  Defines the core event types used throughout Threveal for representing
 *  scheduler migration events and hardware performance counter samples.
 */

#ifndef THREVEAL_CORE_EVENTS_HPP_
#define THREVEAL_CORE_EVENTS_HPP_

#include "threveal/core/types.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace threveal::core
{

// Forward declaration to avoid circular dependency
class TopologyMap;

/**
 *  Maximum length of a process/thread command name.
 *
 *  Linux kernel limits comm to 16 bytes including null terminator.
 */
inline constexpr std::size_t kMaxCommLength = 16;

/**
 *  Classification of migration events by source and destination core types.
 *
 *  Used to categorize scheduler migrations and analyze their performance impact.
 */
enum class MigrationType : std::uint8_t
{
    /**
     *  Migration type could not be determined.
     */
    kUnknown = 0,

    /**
     *  Migration from P-core to P-core.
     */
    kPToP = 1,

    /**
     *  Migration from P-core to E-core (potential performance degradation).
     */
    kPToE = 2,

    /**
     *  Migration from E-core to P-core (potential performance improvement).
     */
    kEToP = 3,

    /**
     *  Migration from E-core to E-core.
     */
    kEToE = 4,
};

/**
 *  Converts a MigrationType to its human-readable string representation.
 *
 *  @param      type  The migration type to convert.
 *  @return     A string view containing "P→P", "P→E", "E→P", "E→E", or "Unknown".
 */
[[nodiscard]] constexpr auto toString(MigrationType type) noexcept -> std::string_view
{
    switch (type)
    {
        case MigrationType::kPToP:
            return "P→P";
        case MigrationType::kPToE:
            return "P→E";
        case MigrationType::kEToP:
            return "E→P";
        case MigrationType::kEToE:
            return "E→E";
        case MigrationType::kUnknown:
            return "Unknown";
    }
    return "Invalid";
}

/**
 *  Represents a scheduler migration event captured from the kernel.
 *
 *  This structure mirrors the data captured by the eBPF program attached
 *  to the sched:sched_migrate_task tracepoint.
 */
struct MigrationEvent
{
    /**
     *  Timestamp when the migration occurred (nanoseconds since boot).
     */
    std::uint64_t timestamp_ns;

    /**
     *  Process ID of the migrated task.
     */
    std::uint32_t pid;

    /**
     *  Thread ID of the migrated task.
     */
    std::uint32_t tid;

    /**
     *  Source CPU ID (where the task was running before migration).
     */
    CpuId src_cpu;

    /**
     *  Destination CPU ID (where the task is running after migration).
     */
    CpuId dst_cpu;

    /**
     *  Command name of the migrated task (may be truncated).
     */
    std::array<char, kMaxCommLength> comm;

    /**
     *  Returns the command name as a string view.
     *
     *  The returned view is valid only while this MigrationEvent exists.
     *
     *  @return     A string view of the command name (null-terminated).
     */
    [[nodiscard]] auto commAsStringView() const noexcept -> std::string_view
    {
        // Find null terminator or use full length
        auto len = std::char_traits<char>::length(comm.data());
        return {comm.data(), std::min(len, comm.size())};
    }
};

/**
 *  Represents a hardware performance counter sample.
 *
 *  PMU samples are collected periodically and correlated with migration
 *  events to measure the performance impact of core migrations.
 */
struct PmuSample
{
    /**
     *  Timestamp when the sample was collected (nanoseconds since boot).
     */
    std::uint64_t timestamp_ns;

    /**
     *  Thread ID this sample belongs to.
     */
    std::uint32_t tid;

    /**
     *  CPU ID where the thread was running when sampled.
     */
    CpuId cpu_id;

    /**
     *  Number of retired instructions since last sample.
     */
    std::uint64_t instructions;

    /**
     *  Number of CPU cycles elapsed since last sample.
     */
    std::uint64_t cycles;

    /**
     *  Number of last-level cache load misses since last sample.
     */
    std::uint64_t llc_misses;

    /**
     *  Number of last-level cache load references since last sample.
     */
    std::uint64_t llc_references;

    /**
     *  Number of branch mispredictions since last sample.
     */
    std::uint64_t branch_misses;

    /**
     *  Computes the Instructions Per Cycle (IPC) for this sample.
     *
     *  @return     IPC value, or 0.0 if cycles is zero.
     */
    [[nodiscard]] constexpr auto ipc() const noexcept -> double
    {
        if (cycles == 0)
        {
            return 0.0;
        }
        return static_cast<double>(instructions) / static_cast<double>(cycles);
    }

    /**
     *  Computes the LLC miss rate for this sample.
     *
     *  @return     Miss rate (0.0 to 1.0), or 0.0 if no references.
     */
    [[nodiscard]] constexpr auto llcMissRate() const noexcept -> double
    {
        if (llc_references == 0)
        {
            return 0.0;
        }
        return static_cast<double>(llc_misses) / static_cast<double>(llc_references);
    }
};

/**
 *  Classifies a migration event by determining source and destination core types.
 *
 *  Uses the provided TopologyMap to look up core types for the source and
 *  destination CPUs and returns the appropriate MigrationType.
 *
 *  @param      event     The migration event to classify.
 *  @param      topology  The topology map for core type lookups.
 *  @return     The classified migration type, or kUnknown if either CPU
 *              is not found in the topology map.
 */
[[nodiscard]] auto classifyMigration(const MigrationEvent& event, const TopologyMap& topology)
    -> MigrationType;

}  // namespace threveal::core

#endif  // THREVEAL_CORE_EVENTS_HPP_

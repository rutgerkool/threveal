/**
 *  @file       event_store.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Storage and querying of migration and PMU events.
 *
 *  Provides efficient storage for events captured during profiling and
 *  supports queries for analysis including time-range filtering,
 *  per-thread filtering, and migration-PMU correlation.
 */

#ifndef THREVEAL_ANALYSIS_EVENT_STORE_HPP_
#define THREVEAL_ANALYSIS_EVENT_STORE_HPP_

#include "threveal/core/events.hpp"
#include "threveal/core/types.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace threveal::analysis
{

/**
 *  Stores migration events and PMU samples for analysis.
 *
 *  EventStore provides efficient storage and querying of profiling data.
 *  Events are stored in insertion order, which should correspond to
 *  chronological order during normal operation.
 *
 *  Thread-safety: This class is NOT thread-safe. External synchronization
 *  is required if accessed from multiple threads.
 */
class EventStore
{
  public:
    /**
     *  Constructs an empty EventStore.
     */
    EventStore() = default;

    /**
     *  Adds a migration event to the store.
     *
     *  @param      event  The migration event to store.
     */
    void addMigration(core::MigrationEvent event);

    /**
     *  Adds a PMU sample to the store.
     *
     *  @param      sample  The PMU sample to store.
     */
    void addPmuSample(core::PmuSample sample);

    /**
     *  Returns a view of all stored migration events.
     *
     *  @return     A span of all migration events in insertion order.
     */
    [[nodiscard]] auto allMigrations() const noexcept -> std::span<const core::MigrationEvent>;

    /**
     *  Returns a view of all stored PMU samples.
     *
     *  @return     A span of all PMU samples in insertion order.
     */
    [[nodiscard]] auto allPmuSamples() const noexcept -> std::span<const core::PmuSample>;

    /**
     *  Returns all migrations for a specific thread.
     *
     *  @param      tid  The thread ID to filter by.
     *  @return     A vector of migrations for the specified thread.
     */
    [[nodiscard]] auto migrationsForThread(std::uint32_t tid) const
        -> std::vector<core::MigrationEvent>;

    /**
     *  Returns all migrations within a time range.
     *
     *  @param      start_ns  Start of time range (inclusive), nanoseconds since boot.
     *  @param      end_ns    End of time range (inclusive), nanoseconds since boot.
     *  @return     A vector of migrations within the specified range.
     */
    [[nodiscard]] auto migrationsInRange(std::uint64_t start_ns, std::uint64_t end_ns) const
        -> std::vector<core::MigrationEvent>;

    /**
     *  Returns all PMU samples for a specific thread.
     *
     *  @param      tid  The thread ID to filter by.
     *  @return     A vector of PMU samples for the specified thread.
     */
    [[nodiscard]] auto pmuSamplesForThread(std::uint32_t tid) const -> std::vector<core::PmuSample>;

    /**
     *  Finds the PMU sample closest to and before a migration event.
     *
     *  Searches for the PMU sample with the same thread ID that has the
     *  largest timestamp less than or equal to the migration timestamp.
     *
     *  @param      migration  The migration event to correlate.
     *  @return     The closest PMU sample before the migration, or std::nullopt
     *              if no suitable sample exists.
     */
    [[nodiscard]] auto pmuBeforeMigration(const core::MigrationEvent& migration) const
        -> std::optional<core::PmuSample>;

    /**
     *  Finds the PMU sample closest to and after a migration event.
     *
     *  Searches for the PMU sample with the same thread ID that has the
     *  smallest timestamp greater than or equal to the migration timestamp.
     *
     *  @param      migration  The migration event to correlate.
     *  @return     The closest PMU sample after the migration, or std::nullopt
     *              if no suitable sample exists.
     */
    [[nodiscard]] auto pmuAfterMigration(const core::MigrationEvent& migration) const
        -> std::optional<core::PmuSample>;

    /**
     *  Returns the number of stored migration events.
     *
     *  @return     The count of migration events.
     */
    [[nodiscard]] auto migrationCount() const noexcept -> std::size_t;

    /**
     *  Returns the number of stored PMU samples.
     *
     *  @return     The count of PMU samples.
     */
    [[nodiscard]] auto pmuSampleCount() const noexcept -> std::size_t;

    /**
     *  Removes all stored events.
     */
    void clear() noexcept;

  private:
    std::vector<core::MigrationEvent> migrations_;
    std::vector<core::PmuSample> pmu_samples_;
};

}  // namespace threveal::analysis

#endif  // THREVEAL_ANALYSIS_EVENT_STORE_HPP_

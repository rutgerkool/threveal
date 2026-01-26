/**
 *  @file       event_store.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Implementation of event storage and querying.
 */

#include "threveal/analysis/event_store.hpp"

#include "threveal/core/events.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <vector>

namespace threveal::analysis
{

void EventStore::addMigration(core::MigrationEvent event)
{
    // Maintain sorted order by timestamp for efficient time-range queries.
    // We use lower_bound to find the first element with timestamp >= event's timestamp.
    // Inserting at this position keeps the vector sorted in ascending order.
    // Complexity: O(log n) for search + O(n) for insertion (vector shift).
    // This trade-off favors read-heavy workloads where queries outnumber insertions.
    auto insertion_point = std::ranges::lower_bound(migrations_, event.timestamp_ns, {},
                                                    [](const core::MigrationEvent& existing)
                                                    {
                                                        return existing.timestamp_ns;
                                                    });

    migrations_.insert(insertion_point, event);
}

void EventStore::addPmuSample(core::PmuSample sample)
{
    pmu_samples_.push_back(sample);
}

auto EventStore::allMigrations() const noexcept -> std::span<const core::MigrationEvent>
{
    return migrations_;
}

auto EventStore::allPmuSamples() const noexcept -> std::span<const core::PmuSample>
{
    return pmu_samples_;
}

auto EventStore::migrationsForThread(std::uint32_t tid) const -> std::vector<core::MigrationEvent>
{
    std::vector<core::MigrationEvent> result;

    // Linear scan required since we're filtering by tid, not timestamp.
    // Migrations are sorted by timestamp, not by thread ID.
    for (const auto& migration : migrations_)
    {
        if (migration.tid == tid)
        {
            result.push_back(migration);
        }
    }

    return result;
}

auto EventStore::migrationsInRange(std::uint64_t start_ns, std::uint64_t end_ns) const
    -> std::vector<core::MigrationEvent>
{
    std::vector<core::MigrationEvent> result;

    // Binary search to find the first migration with timestamp >= start_ns.
    // Since migrations are sorted by timestamp, this gives us O(log n) lookup
    // to the start of our range, instead of scanning from the beginning.
    auto range_start = std::ranges::lower_bound(migrations_, start_ns, {},
                                                [](const core::MigrationEvent& event)
                                                {
                                                    return event.timestamp_ns;
                                                });

    // Iterate from range_start until we exceed end_ns or reach the end.
    // We break early once we pass end_ns since the vector is sorted.
    for (auto it = range_start; it != migrations_.end(); ++it)
    {
        if (it->timestamp_ns > end_ns)
        {
            // Past the end of our range; no need to check remaining elements
            break;
        }
        result.push_back(*it);
    }

    return result;
}

auto EventStore::pmuSamplesForThread(std::uint32_t tid) const -> std::vector<core::PmuSample>
{
    std::vector<core::PmuSample> result;

    // Linear scan to filter by thread ID
    for (const auto& sample : pmu_samples_)
    {
        if (sample.tid == tid)
        {
            result.push_back(sample);
        }
    }

    return result;
}

auto EventStore::pmuBeforeMigration(const core::MigrationEvent& migration) const
    -> std::optional<core::PmuSample>
{
    std::optional<core::PmuSample> best;

    // Linear scan to find the closest PMU sample before (or at) migration time.
    // TODO: Optimize with binary search once PMU samples are sorted.
    for (const auto& sample : pmu_samples_)
    {
        // Must be same thread and at or before migration time
        if (sample.tid != migration.tid || sample.timestamp_ns > migration.timestamp_ns)
        {
            continue;
        }

        // Keep the sample closest to migration time (largest timestamp <= migration)
        if (!best || sample.timestamp_ns > best->timestamp_ns)
        {
            best = sample;
        }
    }

    return best;
}

auto EventStore::pmuAfterMigration(const core::MigrationEvent& migration) const
    -> std::optional<core::PmuSample>
{
    std::optional<core::PmuSample> best;

    // Linear scan to find the closest PMU sample after (or at) migration time.
    // TODO: Optimize with binary search once PMU samples are sorted.
    for (const auto& sample : pmu_samples_)
    {
        // Must be same thread and at or after migration time
        if (sample.tid != migration.tid || sample.timestamp_ns < migration.timestamp_ns)
        {
            continue;
        }

        // Keep the sample closest to migration time (smallest timestamp >= migration)
        if (!best || sample.timestamp_ns < best->timestamp_ns)
        {
            best = sample;
        }
    }

    return best;
}

auto EventStore::migrationCount() const noexcept -> std::size_t
{
    return migrations_.size();
}

auto EventStore::pmuSampleCount() const noexcept -> std::size_t
{
    return pmu_samples_.size();
}

void EventStore::clear() noexcept
{
    migrations_.clear();
    pmu_samples_.clear();
}

}  // namespace threveal::analysis

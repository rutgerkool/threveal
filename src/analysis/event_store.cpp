/**
 *  @file       event_store.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Implementation of event storage and querying.
 */

#include "threveal/analysis/event_store.hpp"

#include "threveal/core/events.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace threveal::analysis
{

void EventStore::addMigration(core::MigrationEvent event)
{
    migrations_.push_back(event);
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

    for (const auto& migration : migrations_)
    {
        if (migration.timestamp_ns >= start_ns && migration.timestamp_ns <= end_ns)
        {
            result.push_back(migration);
        }
    }

    return result;
}

auto EventStore::pmuSamplesForThread(std::uint32_t tid) const -> std::vector<core::PmuSample>
{
    std::vector<core::PmuSample> result;

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

    for (const auto& sample : pmu_samples_)
    {
        // Must be same thread and at or before migration time
        if (sample.tid != migration.tid || sample.timestamp_ns > migration.timestamp_ns)
        {
            continue;
        }

        // Keep the sample closest to migration time
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

    for (const auto& sample : pmu_samples_)
    {
        // Must be same thread and at or after migration time
        if (sample.tid != migration.tid || sample.timestamp_ns < migration.timestamp_ns)
        {
            continue;
        }

        // Keep the sample closest to migration time
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

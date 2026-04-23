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
    auto insertion_point = std::ranges::lower_bound(migrations_, event.timestamp_ns, {},
                                                    [](const core::MigrationEvent& existing)
                                                    {
                                                        return existing.timestamp_ns;
                                                    });

    migrations_.insert(insertion_point, event);
}

void EventStore::addPmuSample(core::PmuSample sample)
{
    auto insertion_point = std::ranges::lower_bound(pmu_samples_, sample.timestamp_ns, {},
                                                    [](const core::PmuSample& existing)
                                                    {
                                                        return existing.timestamp_ns;
                                                    });

    pmu_samples_.insert(insertion_point, sample);
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

    auto range_start = std::ranges::lower_bound(migrations_, start_ns, {},
                                                [](const core::MigrationEvent& event)
                                                {
                                                    return event.timestamp_ns;
                                                });

    for (auto it = range_start; it != migrations_.end(); ++it)
    {
        if (it->timestamp_ns > end_ns)
        {
            break;
        }
        result.push_back(*it);
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
    if (pmu_samples_.empty())
    {
        return std::nullopt;
    }

    auto upper = std::ranges::upper_bound(pmu_samples_, migration.timestamp_ns, {},
                                          [](const core::PmuSample& sample)
                                          {
                                              return sample.timestamp_ns;
                                          });

    for (auto it = std::make_reverse_iterator(upper); it != pmu_samples_.rend(); ++it)
    {
        if (it->tid == migration.tid)
        {
            return *it;
        }
    }

    return std::nullopt;
}

auto EventStore::pmuAfterMigration(const core::MigrationEvent& migration) const
    -> std::optional<core::PmuSample>
{
    if (pmu_samples_.empty())
    {
        return std::nullopt;
    }

    auto lower = std::ranges::lower_bound(pmu_samples_, migration.timestamp_ns, {},
                                          [](const core::PmuSample& sample)
                                          {
                                              return sample.timestamp_ns;
                                          });

    for (auto it = lower; it != pmu_samples_.end(); ++it)
    {
        if (it->tid == migration.tid)
        {
            return *it;
        }
    }

    return std::nullopt;
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

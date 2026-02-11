/**
 *  @file       migration_analyzer.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Implementation of migration-PMU correlation analysis.
 */

#include "threveal/analysis/migration_analyzer.hpp"

#include "threveal/core/events.hpp"

#include <vector>

namespace threveal::analysis
{

MigrationAnalyzer::MigrationAnalyzer(const EventStore& store,
                                     const core::TopologyMap& topology) noexcept
    : store_(store), topology_(topology)
{
}

auto MigrationAnalyzer::analyze() const -> std::vector<MigrationImpact>
{
    auto migrations = store_.allMigrations();

    std::vector<MigrationImpact> impacts;
    impacts.reserve(migrations.size());

    for (const auto& migration : migrations)
    {
        impacts.push_back(computeImpact(migration));
    }

    return impacts;
}

auto MigrationAnalyzer::computeImpact(const core::MigrationEvent& migration) const
    -> MigrationImpact
{
    auto type = core::classifyMigration(migration, topology_);

    auto sample_before = store_.pmuBeforeMigration(migration);
    auto sample_after = store_.pmuAfterMigration(migration);

    if (!sample_before || !sample_after)
    {
        return MigrationImpact{
            .event = migration,
            .type = type,
            .ipc_delta = 0.0,
            .cache_miss_delta = 0.0,
            .confidence = 0.0,
        };
    }

    double ipc_delta = sample_after->ipc() - sample_before->ipc();
    double cache_miss_delta = sample_after->llcMissRate() - sample_before->llcMissRate();

    return MigrationImpact{
        .event = migration,
        .type = type,
        .ipc_delta = ipc_delta,
        .cache_miss_delta = cache_miss_delta,
        .confidence = 1.0,
    };
}

}  // namespace threveal::analysis

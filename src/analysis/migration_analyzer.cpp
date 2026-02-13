/**
 *  @file       migration_analyzer.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Implementation of migration-PMU correlation analysis.
 */

#include "threveal/analysis/migration_analyzer.hpp"

#include "threveal/core/events.hpp"

#include <cmath>
#include <vector>

namespace threveal::analysis
{

MigrationAnalyzer::MigrationAnalyzer(const EventStore& store,
                                     const core::TopologyMap& topology) noexcept
    : store_(store), topology_(topology)
{
}

void MigrationAnalyzer::setMaxSampleGap(std::uint64_t gap_ns) noexcept
{
    max_sample_gap_ns_ = gap_ns;
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

    auto gap_before_ns = migration.timestamp_ns - sample_before->timestamp_ns;
    auto gap_after_ns = sample_after->timestamp_ns - migration.timestamp_ns;

    if (gap_before_ns > max_sample_gap_ns_ || gap_after_ns > max_sample_gap_ns_)
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

    double confidence = calculateConfidence(gap_before_ns, gap_after_ns);

    return MigrationImpact{
        .event = migration,
        .type = type,
        .ipc_delta = ipc_delta,
        .cache_miss_delta = cache_miss_delta,
        .confidence = confidence,
    };
}

auto MigrationAnalyzer::calculateConfidence(std::uint64_t gap_before_ns,
                                            std::uint64_t gap_after_ns) const noexcept -> double
{
    auto max_gap = std::max(gap_before_ns, gap_after_ns);

    if (max_gap >= max_sample_gap_ns_)
    {
        return 0.0;
    }

    constexpr double kDecayRate = 3.0;
    double normalized_gap = static_cast<double>(max_gap) / static_cast<double>(max_sample_gap_ns_);

    return std::exp(-kDecayRate * normalized_gap);
}

}  // namespace threveal::analysis

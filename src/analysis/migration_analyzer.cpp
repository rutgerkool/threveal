/**
 *  @file       migration_analyzer.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Implementation of migration-PMU correlation analysis.
 */

#include "threveal/analysis/migration_analyzer.hpp"

#include "threveal/core/events.hpp"

#include <algorithm>
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
            .branch_miss_delta = 0.0,
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
            .branch_miss_delta = 0.0,
            .confidence = 0.0,
        };
    }

    double ipc_delta = sample_after->ipc() - sample_before->ipc();
    double cache_miss_delta = sample_after->llcMissRate() - sample_before->llcMissRate();

    double branch_miss_before = (sample_before->instructions > 0)
                                    ? static_cast<double>(sample_before->branch_misses) /
                                          static_cast<double>(sample_before->instructions)
                                    : 0.0;
    double branch_miss_after = (sample_after->instructions > 0)
                                   ? static_cast<double>(sample_after->branch_misses) /
                                         static_cast<double>(sample_after->instructions)
                                   : 0.0;
    double branch_miss_delta = branch_miss_after - branch_miss_before;

    double confidence = calculateConfidence(gap_before_ns, gap_after_ns);

    return MigrationImpact{
        .event = migration,
        .type = type,
        .ipc_delta = ipc_delta,
        .cache_miss_delta = cache_miss_delta,
        .branch_miss_delta = branch_miss_delta,
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

auto MigrationAnalyzer::aggregateByType(const std::vector<MigrationImpact>& impacts) const
    -> std::vector<MigrationTypeStats>
{
    struct Accumulator
    {
        std::uint32_t count = 0;
        double ipc_sum = 0.0;
        double cache_miss_sum = 0.0;
        double branch_miss_sum = 0.0;
        double confidence_sum = 0.0;
    };

    constexpr std::size_t kTypeCount = 5;
    std::array<Accumulator, kTypeCount> accumulators{};

    for (const auto& impact : impacts)
    {
        if (impact.confidence <= 0.0)
        {
            continue;
        }

        auto idx = static_cast<std::size_t>(impact.type);
        if (idx >= kTypeCount)
        {
            continue;
        }

        auto& acc = accumulators.at(idx);
        ++acc.count;
        acc.ipc_sum += impact.ipc_delta;
        acc.cache_miss_sum += impact.cache_miss_delta;
        acc.branch_miss_sum += impact.branch_miss_delta;
        acc.confidence_sum += impact.confidence;
    }

    std::vector<MigrationTypeStats> result;

    for (std::size_t i = 0; i < kTypeCount; ++i)
    {
        const auto& acc = accumulators.at(i);
        if (acc.count == 0)
        {
            continue;
        }

        auto divisor = static_cast<double>(acc.count);
        result.push_back(MigrationTypeStats{
            .type = static_cast<core::MigrationType>(i),
            .count = acc.count,
            .avg_ipc_delta = acc.ipc_sum / divisor,
            .avg_cache_miss_delta = acc.cache_miss_sum / divisor,
            .avg_branch_miss_delta = acc.branch_miss_sum / divisor,
            .avg_confidence = acc.confidence_sum / divisor,
        });
    }

    return result;
}

}  // namespace threveal::analysis

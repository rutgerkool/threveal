/**
 *  @file       migration_analyzer.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Implementation of migration-PMU correlation analysis.
 */

#include "threveal/analysis/migration_analyzer.hpp"

#include "threveal/analysis/event_store.hpp"
#include "threveal/core/events.hpp"
#include "threveal/core/topology.hpp"
#include "threveal/core/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace threveal::analysis
{

MigrationAnalyzer::MigrationAnalyzer(const EventStore& store,
                                     const core::TopologyMap& topology) noexcept
    : store_(&store), topology_(&topology)
{
}

void MigrationAnalyzer::setMaxSampleGap(std::uint64_t gap_ns) noexcept
{
    max_sample_gap_ns_ = gap_ns;
}

void MigrationAnalyzer::setMinConfidence(double threshold) noexcept
{
    min_confidence_ = threshold;
}

auto MigrationAnalyzer::analyze() const -> AnalysisResult
{
    auto migrations = store_->allMigrations();

    // Compute per-migration performance impact
    std::vector<MigrationImpact> impacts;
    impacts.reserve(migrations.size());

    std::uint32_t correlated = 0;

    for (const auto& migration : migrations)
    {
        auto impact = computeImpact(migration);
        if (impact.confidence >= min_confidence_)
        {
            ++correlated;
        }
        impacts.push_back(impact);
    }

    // Aggregate into per-type and per-thread statistics
    auto type_stats = aggregateByType(impacts);
    auto thread_stats = aggregateByThread(impacts);

    return AnalysisResult{
        .impacts = std::move(impacts),
        .type_stats = std::move(type_stats),
        .thread_stats = std::move(thread_stats),
        .total_migrations = static_cast<std::uint32_t>(migrations.size()),
        .correlated_migrations = correlated,
    };
}

auto MigrationAnalyzer::computeImpact(const core::MigrationEvent& migration) const
    -> MigrationImpact
{
    // Classify migration type using topology
    auto type = core::classifyMigration(migration, *topology_);

    // Find closest PMU samples on each side of the migration boundary
    auto sample_before = store_->pmuBeforeMigration(migration);
    auto sample_after = store_->pmuAfterMigration(migration);

    // If either sample is missing, return a zero-confidence impact
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

    // Compute time gaps between samples and migration
    auto gap_before_ns = migration.timestamp_ns - sample_before->timestamp_ns;
    auto gap_after_ns = sample_after->timestamp_ns - migration.timestamp_ns;

    // Reject samples that are too far from the migration event
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

    // Compute performance deltas across the migration boundary
    double ipc_delta = sample_after->ipc() - sample_before->ipc();
    double cache_miss_delta = sample_after->llcMissRate() - sample_before->llcMissRate();

    // Branch miss rate: misses per instruction (normalized for comparison)
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
    // Use the larger of the two gaps as the dominant uncertainty factor.
    // A migration bracketed tightly by samples is more trustworthy.
    auto max_gap = std::max(gap_before_ns, gap_after_ns);

    if (max_gap >= max_sample_gap_ns_)
    {
        return 0.0;
    }

    // Exponential decay: confidence = exp(-3 * gap / max_gap)
    // At gap=0: confidence ≈ 1.0
    // At gap=max/3: confidence ≈ 0.37
    // At gap=max: confidence ≈ 0.05 (essentially zero)
    constexpr double kDecayRate = 3.0;
    double normalized_gap = static_cast<double>(max_gap) / static_cast<double>(max_sample_gap_ns_);

    return std::exp(-kDecayRate * normalized_gap);
}

auto MigrationAnalyzer::aggregateByType(const std::vector<MigrationImpact>& impacts) const
    -> std::vector<MigrationTypeStats>
{
    // Accumulators for each migration type
    struct Accumulator
    {
        std::uint32_t count = 0;
        double ipc_sum = 0.0;
        double cache_miss_sum = 0.0;
        double branch_miss_sum = 0.0;
        double confidence_sum = 0.0;
    };

    // Use an array indexed by MigrationType enum value (0-4)
    constexpr std::size_t kTypeCount = 5;
    std::array<Accumulator, kTypeCount> accumulators{};

    for (const auto& impact : impacts)
    {
        // Only include impacts that meet the confidence threshold
        if (impact.confidence < min_confidence_)
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

    // Convert accumulators to MigrationTypeStats, skipping types with zero count
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

auto MigrationAnalyzer::aggregateByThread(const std::vector<MigrationImpact>& impacts) const
    -> std::vector<ThreadStatistics>
{
    // Per-thread accumulator for building statistics
    struct ThreadAccumulator
    {
        std::uint32_t pid = 0;
        std::string comm;
        std::uint32_t total = 0;
        std::uint32_t p_to_e = 0;
        std::uint32_t e_to_p = 0;
        std::uint32_t p_to_p = 0;
        std::uint32_t e_to_e = 0;
        // Only accumulate from impacts meeting confidence threshold
        double p_to_e_ipc_sum = 0.0;
        std::uint32_t p_to_e_confident = 0;
        double e_to_p_ipc_sum = 0.0;
        std::uint32_t e_to_p_confident = 0;
        double cache_miss_sum = 0.0;
        std::uint32_t cross_type_confident = 0;
    };

    std::unordered_map<std::uint32_t, ThreadAccumulator> thread_map;

    for (const auto& impact : impacts)
    {
        auto tid = impact.event.tid;
        auto& acc = thread_map[tid];

        // Set identity fields on first encounter
        if (acc.total == 0)
        {
            acc.pid = impact.event.pid;
            acc.comm = std::string(impact.event.commAsStringView());
        }

        // Count all migrations regardless of confidence
        ++acc.total;

        switch (impact.type)
        {
            case core::MigrationType::kPToE:
                ++acc.p_to_e;
                break;
            case core::MigrationType::kEToP:
                ++acc.e_to_p;
                break;
            case core::MigrationType::kPToP:
                ++acc.p_to_p;
                break;
            case core::MigrationType::kEToE:
                ++acc.e_to_e;
                break;
            case core::MigrationType::kUnknown:
                break;
        }

        // Accumulate performance deltas only for confident measurements
        if (impact.confidence < min_confidence_)
        {
            continue;
        }

        bool is_cross_type = (impact.type == core::MigrationType::kPToE ||
                              impact.type == core::MigrationType::kEToP);

        if (impact.type == core::MigrationType::kPToE)
        {
            acc.p_to_e_ipc_sum += impact.ipc_delta;
            ++acc.p_to_e_confident;
        }
        else if (impact.type == core::MigrationType::kEToP)
        {
            acc.e_to_p_ipc_sum += impact.ipc_delta;
            ++acc.e_to_p_confident;
        }

        if (is_cross_type)
        {
            acc.cache_miss_sum += impact.cache_miss_delta;
            ++acc.cross_type_confident;
        }
    }

    // Convert accumulators to ThreadStatistics
    std::vector<ThreadStatistics> result;
    result.reserve(thread_map.size());

    for (const auto& [tid, acc] : thread_map)
    {
        double avg_ipc_loss = (acc.p_to_e_confident > 0)
                                  ? acc.p_to_e_ipc_sum / static_cast<double>(acc.p_to_e_confident)
                                  : 0.0;

        double avg_ipc_gain = (acc.e_to_p_confident > 0)
                                  ? acc.e_to_p_ipc_sum / static_cast<double>(acc.e_to_p_confident)
                                  : 0.0;

        double avg_cache_delta =
            (acc.cross_type_confident > 0)
                ? acc.cache_miss_sum / static_cast<double>(acc.cross_type_confident)
                : 0.0;

        result.push_back(ThreadStatistics{
            .tid = tid,
            .pid = acc.pid,
            .comm = acc.comm,
            .total_migrations = acc.total,
            .p_to_e_migrations = acc.p_to_e,
            .e_to_p_migrations = acc.e_to_p,
            .p_to_p_migrations = acc.p_to_p,
            .e_to_e_migrations = acc.e_to_e,
            .avg_ipc_loss_on_p_to_e = avg_ipc_loss,
            .avg_ipc_gain_on_e_to_p = avg_ipc_gain,
            .avg_cache_miss_delta = avg_cache_delta,
        });
    }

    // Sort by total migrations descending for convenient consumption
    std::ranges::sort(result,
                      [](const ThreadStatistics& lhs, const ThreadStatistics& rhs)
                      {
                          return lhs.total_migrations > rhs.total_migrations;
                      });

    return result;
}

}  // namespace threveal::analysis

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
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace threveal::analysis
{

namespace
{

/**
 *  Builds an impact for a migration that could not be correlated with PMU data.
 *
 *  @param      migration  The migration event.
 *  @param      type       The classified migration type.
 *  @return     An impact with zero deltas and zero confidence.
 */
[[nodiscard]] auto makeUncorrelatedImpact(const core::MigrationEvent& migration,
                                          core::MigrationType type) -> MigrationImpact
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

/**
 *  Running totals for one thread while aggregating impacts.
 */
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

/**
 *  Counts one migration in the thread's per-type tallies.
 */
void countMigrationType(ThreadAccumulator& acc, core::MigrationType type) noexcept
{
    switch (type)
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
}

/**
 *  Adds the performance deltas of a cross-type migration; other types are ignored.
 */
void addCrossTypeDeltas(ThreadAccumulator& acc, const MigrationImpact& impact) noexcept
{
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
    else
    {
        return;
    }

    acc.cache_miss_sum += impact.cache_miss_delta;
    ++acc.cross_type_confident;
}

/**
 *  Adds one migration impact to its thread's accumulator.
 *
 *  @param      acc             The accumulator of the impact's thread.
 *  @param      impact          The migration impact to add.
 *  @param      min_confidence  Minimum confidence for the deltas to be included.
 */
void addImpact(ThreadAccumulator& acc, const MigrationImpact& impact, double min_confidence)
{
    if (acc.total == 0)
    {
        acc.pid = impact.event.pid;
        acc.comm = std::string(impact.event.commAsStringView());
    }

    ++acc.total;
    countMigrationType(acc, impact.type);

    // Every migration is counted, but deltas only from confident measurements
    if (impact.confidence < min_confidence)
    {
        return;
    }
    addCrossTypeDeltas(acc, impact);
}

/**
 *  Averages a sum over a count, or returns 0.0 when there is nothing to average.
 */
constexpr auto average(double sum, std::uint32_t count) noexcept -> double
{
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

/**
 *  Builds the final statistics of one thread from its accumulator.
 *
 *  @param      tid  The thread ID.
 *  @param      acc  The thread's accumulated totals.
 *  @return     The thread's statistics.
 */
auto toThreadStatistics(std::uint32_t tid, const ThreadAccumulator& acc) -> ThreadStatistics
{
    return ThreadStatistics{
        .tid = tid,
        .pid = acc.pid,
        .comm = acc.comm,
        .total_migrations = acc.total,
        .p_to_e_migrations = acc.p_to_e,
        .e_to_p_migrations = acc.e_to_p,
        .p_to_p_migrations = acc.p_to_p,
        .e_to_e_migrations = acc.e_to_e,
        .avg_ipc_loss_on_p_to_e = average(acc.p_to_e_ipc_sum, acc.p_to_e_confident),
        .avg_ipc_gain_on_e_to_p = average(acc.e_to_p_ipc_sum, acc.e_to_p_confident),
        .avg_cache_miss_delta = average(acc.cache_miss_sum, acc.cross_type_confident),
    };
}

}  // namespace

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
        return makeUncorrelatedImpact(migration, type);
    }

    // Compute time gaps between samples and migration
    auto gap_before_ns = migration.timestamp_ns - sample_before->timestamp_ns;
    auto gap_after_ns = sample_after->timestamp_ns - migration.timestamp_ns;

    // Reject samples that are too far from the migration event
    if (gap_before_ns > max_sample_gap_ns_ || gap_after_ns > max_sample_gap_ns_)
    {
        return makeUncorrelatedImpact(migration, type);
    }

    // Compute performance deltas across the migration boundary
    double ipc_delta = sample_after->ipc() - sample_before->ipc();
    double cache_miss_delta = sample_after->llcMissRate() - sample_before->llcMissRate();
    double branch_miss_delta = sample_after->branchMissRate() - sample_before->branchMissRate();

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
    auto max_gap = std::max(gap_before_ns, gap_after_ns);

    if (max_gap >= max_sample_gap_ns_)
    {
        return 0.0;
    }

    // Exponential decay: confidence = exp(-3 * gap / max_gap)
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

    // Use an array indexed by MigrationType value
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
    std::unordered_map<std::uint32_t, ThreadAccumulator> thread_map;

    for (const auto& impact : impacts)
    {
        addImpact(thread_map[impact.event.tid], impact, min_confidence_);
    }

    std::vector<ThreadStatistics> result;
    result.reserve(thread_map.size());

    for (const auto& [tid, acc] : thread_map)
    {
        result.push_back(toThreadStatistics(tid, acc));
    }

    // Most migrations first, ties by tid so the order does not depend on hash map iteration
    std::ranges::sort(result,
                      [](const ThreadStatistics& lhs, const ThreadStatistics& rhs)
                      {
                          if (lhs.total_migrations != rhs.total_migrations)
                          {
                              return lhs.total_migrations > rhs.total_migrations;
                          }
                          return lhs.tid < rhs.tid;
                      });

    return result;
}

}  // namespace threveal::analysis

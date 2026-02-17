/**
 *  @file       migration_analyzer.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Analysis of migration events correlated with PMU performance data.
 */

#ifndef THREVEAL_ANALYSIS_MIGRATION_ANALYZER_HPP_
#define THREVEAL_ANALYSIS_MIGRATION_ANALYZER_HPP_

#include "threveal/analysis/event_store.hpp"
#include "threveal/core/events.hpp"
#include "threveal/core/topology.hpp"
#include "threveal/core/types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace threveal::analysis
{

/**
 *  Performance impact of a single migration event.
 */
struct MigrationImpact
{
    /**
     *  The migration event that was analyzed.
     */
    core::MigrationEvent event;

    /**
     *  Classification of this migration by source and destination core types.
     */
    core::MigrationType type;

    /**
     *  Change in Instructions Per Cycle across the migration boundary.
     *  Negative values indicate performance degradation (fewer instructions
     *  per cycle after migration).
     */
    double ipc_delta;

    /**
     *  Change in LLC miss rate across the migration boundary.
     *  Positive values indicate increased cache pressure (more misses after
     *  migration).
     */
    double cache_miss_delta;

    /**
     *  Change in branch miss rate across the migration boundary.
     *  Positive values indicate increased branch mispredictions after migration.
     */
    double branch_miss_delta;

    /**
     *  Confidence score for this impact measurement (0.0 to 1.0).
     */
    double confidence;
};

/**
 *  Aggregated statistics for a specific migration type.
 */
struct MigrationTypeStats
{
    /**
     *  The migration type these statistics describe.
     */
    core::MigrationType type;

    /**
     *  Total number of migrations of this type.
     */
    std::uint32_t count;

    /**
     *  Average IPC delta across all migrations of this type.
     */
    double avg_ipc_delta;

    /**
     *  Average LLC miss rate delta across all migrations of this type.
     */
    double avg_cache_miss_delta;

    /**
     *  Average branch miss rate delta across all migrations of this type.
     */
    double avg_branch_miss_delta;

    /**
     *  Average confidence score across all migrations of this type.
     */
    double avg_confidence;
};

/**
 *  Per-thread analysis summary combining migration counts and performance impact.
 */
struct ThreadStatistics
{
    /**
     *  Thread ID.
     */
    std::uint32_t tid;

    /**
     *  Process ID.
     */
    std::uint32_t pid;

    /**
     *  Command name of the thread.
     */
    std::string comm;

    /**
     *  Total number of migrations for this thread.
     */
    std::uint32_t total_migrations;

    /**
     *  Number of P-core to E-core migrations.
     */
    std::uint32_t p_to_e_migrations;

    /**
     *  Number of E-core to P-core migrations.
     */
    std::uint32_t e_to_p_migrations;

    /**
     *  Number of P-core to P-core migrations.
     */
    std::uint32_t p_to_p_migrations;

    /**
     *  Number of E-core to E-core migrations.
     */
    std::uint32_t e_to_e_migrations;

    /**
     *  Average IPC loss observed on P→E migrations.
     *  Negative values indicate performance degradation.
     *  Zero if no P→E migrations occurred.
     */
    double avg_ipc_loss_on_p_to_e;

    /**
     *  Average IPC gain observed on E→P migrations.
     *  Positive values indicate performance improvement.
     *  Zero if no E→P migrations occurred.
     */
    double avg_ipc_gain_on_e_to_p;

    /**
     *  Average LLC miss rate delta across all cross-type migrations.
     *  Positive values indicate cache state destruction on migration.
     */
    double avg_cache_miss_delta;
};

/**
 *  Complete analysis results from a profiling session.
 */
struct AnalysisResult
{
    std::vector<MigrationImpact> impacts;
    std::vector<MigrationTypeStats> type_stats;
    std::vector<ThreadStatistics> thread_stats;
    std::uint32_t total_migrations;
    std::uint32_t correlated_migrations;
};

/**
 *  Analyzes migration events correlated with PMU performance data.
 */
class MigrationAnalyzer
{
  public:
    static constexpr std::uint64_t kDefaultMaxSampleGapNs = 10'000'000;
    static constexpr double kDefaultMinConfidence = 0.1;

    MigrationAnalyzer(const EventStore& store, const core::TopologyMap& topology) noexcept;

    [[nodiscard]] auto analyze() const -> AnalysisResult;

    void setMaxSampleGap(std::uint64_t gap_ns) noexcept;
    void setMinConfidence(double threshold) noexcept;

  private:
    [[nodiscard]] auto computeImpact(const core::MigrationEvent& migration) const
        -> MigrationImpact;

    [[nodiscard]] auto calculateConfidence(std::uint64_t gap_before_ns,
                                           std::uint64_t gap_after_ns) const noexcept -> double;

    [[nodiscard]] auto aggregateByType(const std::vector<MigrationImpact>& impacts) const
        -> std::vector<MigrationTypeStats>;

    [[nodiscard]] auto aggregateByThread(const std::vector<MigrationImpact>& impacts) const
        -> std::vector<ThreadStatistics>;

    const EventStore* store_;
    const core::TopologyMap* topology_;
    std::uint64_t max_sample_gap_ns_{kDefaultMaxSampleGapNs};
    double min_confidence_{kDefaultMinConfidence};
};

}  // namespace threveal::analysis

#endif  // THREVEAL_ANALYSIS_MIGRATION_ANALYZER_HPP_

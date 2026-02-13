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
    core::MigrationEvent event;
    core::MigrationType type;
    double ipc_delta;
    double cache_miss_delta;
    double branch_miss_delta;
    double confidence;
};

/**
 *  Aggregated statistics for a specific migration type.
 */
struct MigrationTypeStats
{
    core::MigrationType type;
    std::uint32_t count;
    double avg_ipc_delta;
    double avg_cache_miss_delta;
    double avg_branch_miss_delta;
    double avg_confidence;
};

/**
 *  Per-thread analysis summary combining migration counts and performance impact.
 */
struct ThreadStatistics
{
    std::uint32_t tid;
    std::uint32_t pid;
    std::string comm;
    std::uint32_t total_migrations;
    std::uint32_t p_to_e_migrations;
    std::uint32_t e_to_p_migrations;
    std::uint32_t p_to_p_migrations;
    std::uint32_t e_to_e_migrations;
    double avg_ipc_loss_on_p_to_e;
    double avg_ipc_gain_on_e_to_p;
    double avg_cache_miss_delta;
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

    [[nodiscard]] auto analyze() const -> std::vector<MigrationImpact>;

    void setMaxSampleGap(std::uint64_t gap_ns) noexcept;
    void setMinConfidence(double threshold) noexcept;

  private:
    [[nodiscard]] auto computeImpact(const core::MigrationEvent& migration) const
        -> MigrationImpact;

    [[nodiscard]] auto calculateConfidence(std::uint64_t gap_before_ns,
                                           std::uint64_t gap_after_ns) const noexcept -> double;

    [[nodiscard]] auto aggregateByType(const std::vector<MigrationImpact>& impacts) const
        -> std::vector<MigrationTypeStats>;

    const EventStore& store_;
    const core::TopologyMap& topology_;
    std::uint64_t max_sample_gap_ns_{kDefaultMaxSampleGapNs};
    double min_confidence_{kDefaultMinConfidence};
};

}  // namespace threveal::analysis

#endif  // THREVEAL_ANALYSIS_MIGRATION_ANALYZER_HPP_

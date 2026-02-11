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
    double confidence;
};

/**
 *  Analyzes migration events correlated with PMU performance data.
 */
class MigrationAnalyzer
{
  public:
    MigrationAnalyzer(const EventStore& store, const core::TopologyMap& topology) noexcept;

    [[nodiscard]] auto analyze() const -> std::vector<MigrationImpact>;

  private:
    [[nodiscard]] auto computeImpact(const core::MigrationEvent& migration) const
        -> MigrationImpact;

    const EventStore& store_;
    const core::TopologyMap& topology_;
};

}  // namespace threveal::analysis

#endif  // THREVEAL_ANALYSIS_MIGRATION_ANALYZER_HPP_

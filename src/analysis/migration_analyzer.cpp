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
    return {};
}

}  // namespace threveal::analysis

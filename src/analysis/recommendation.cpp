/**
 *  @file       recommendation.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Implementation of the rule-based thread affinity recommendation engine.
 */

#include "threveal/analysis/recommendation.hpp"

#include "threveal/analysis/migration_analyzer.hpp"

#include <cstdint>
#include <fmt/core.h>
#include <vector>

namespace threveal::analysis
{

RecommendationEngine::RecommendationEngine(std::uint64_t profiling_duration_ns) noexcept
    : profiling_duration_ns_(profiling_duration_ns)
{
}

void RecommendationEngine::setMinMigrations(std::uint32_t min) noexcept
{
    min_migrations_ = min;
}

void RecommendationEngine::setIpcLossThreshold(double threshold) noexcept
{
    ipc_loss_threshold_ = threshold;
}

void RecommendationEngine::setHighMigrationRate(double rate_per_sec) noexcept
{
    high_migration_rate_ = rate_per_sec;
}

}  // namespace threveal::analysis

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

namespace
{

/// Nanoseconds per second as a floating-point constant.
constexpr double kNsPerSec = 1.0e9;

}  // namespace

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

auto RecommendationEngine::computeMigrationRate(std::uint32_t total_migrations) const noexcept
    -> double
{
    // Guard against zero duration to avoid division by zero
    if (profiling_duration_ns_ == 0)
    {
        return 0.0;
    }

    double duration_sec = static_cast<double>(profiling_duration_ns_) / kNsPerSec;
    return static_cast<double>(total_migrations) / duration_sec;
}

auto RecommendationEngine::analyze(const std::vector<ThreadStatistics>& thread_stats) const
    -> std::vector<ThreadRecommendation>
{
    std::vector<ThreadRecommendation> results;
    results.reserve(thread_stats.size());

    for (const auto& stats : thread_stats)
    {
        results.push_back(recommend(stats));
    }

    return results;
}

}  // namespace threveal::analysis

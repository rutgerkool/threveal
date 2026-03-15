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

auto RecommendationEngine::recommend(const ThreadStatistics& stats) const -> ThreadRecommendation
{
    // --- Pre-compute derived metrics used across multiple rules ---

    double rate = computeMigrationRate(stats.total_migrations);

    // Fraction of migrations that move the thread from P-core to E-core
    double p_to_e_fraction = (stats.total_migrations > 0)
                                 ? static_cast<double>(stats.p_to_e_migrations) /
                                       static_cast<double>(stats.total_migrations)
                                 : 0.0;

    // E-core occupancy: number of migrations that arrive at or stay on E-cores
    double e_core_fraction =
        (stats.total_migrations > 0)
            ? static_cast<double>(stats.e_to_e_migrations + stats.p_to_e_migrations) /
                  static_cast<double>(stats.total_migrations)
            : 0.0;

    // Total cross-type migration count
    std::uint32_t cross_type = stats.p_to_e_migrations + stats.e_to_p_migrations;

    ThreadRecommendation result{
        .tid = stats.tid,
        .pid = stats.pid,
        .comm = stats.comm,
        .recommendation = AffinityRecommendation::kNone,
        .explanation = "",
        .migration_rate_per_second = rate,
        .p_to_e_fraction = p_to_e_fraction,
    };

    // Not enough data to draw conclusions
    if (stats.total_migrations < min_migrations_)
    {
        result.explanation = "Insufficient migration data for analysis.";
        return result;
    }

    // Significant P to E fraction with measurable IPC loss
    bool high_p_to_e_fraction = (p_to_e_fraction >= p_to_e_fraction_threshold_);
    bool significant_ipc_loss = (stats.avg_ipc_loss_on_p_to_e < ipc_loss_threshold_);

    if (high_p_to_e_fraction && significant_ipc_loss)
    {
        result.recommendation = AffinityRecommendation::kPinToPCores;
        result.explanation = fmt::format(
            "Thread experiences an average IPC loss of {:.2f} on P→E migrations "
            "({:.0f}% of all migrations). "
            "Pinning to P-cores should eliminate this migration penalty.",
            stats.avg_ipc_loss_on_p_to_e, p_to_e_fraction * 100.0);
        return result;
    }

    // Very high migration frequency
    if (rate >= high_migration_rate_)
    {
        result.recommendation = AffinityRecommendation::kReduceMigrations;
        result.explanation = fmt::format(
            "Thread is migrating at {:.0f} migrations/second. "
            "Excessive migration frequency causes repeated cache-state destruction. "
            "Pinning to a fixed core set should reduce this overhead.",
            rate);
        return result;
    }

    // Thread predominantly on E-cores with no P-core benefit
    bool mostly_on_e_cores = (e_core_fraction > e_core_majority_threshold_);
    bool no_significant_p_core_benefit = (stats.avg_ipc_gain_on_e_to_p < significant_ipc_gain_);

    // Only recommend E-core pinning if the scheduler is actually migrating the thread
    bool is_being_pulled_to_p_cores = (stats.e_to_p_migrations > 0);

    if (mostly_on_e_cores && no_significant_p_core_benefit && is_being_pulled_to_p_cores)
    {
        result.recommendation = AffinityRecommendation::kPinToECores;
        result.explanation = fmt::format(
            "Thread has {:.0f}% E-core activity and gains only {:.2f} IPC "
            "when migrated to a P-core. "
            "Pinning to E-cores eliminates unnecessary migrations without harming throughput.",
            e_core_fraction * 100.0, stats.avg_ipc_gain_on_e_to_p);
        return result;
    }

    // Cross-type activity exists but patterns are ambiguous
    bool has_cross_type_activity = (cross_type > 0);

    if (has_cross_type_activity)
    {
        result.recommendation = AffinityRecommendation::kInvestigateFurther;
        result.explanation = fmt::format(
            "Thread has {} cross-type migration(s) (P→E: {}, E→P: {}) "
            "but patterns are inconclusive. "
            "Manual inspection of the profiling data is recommended.",
            cross_type, stats.p_to_e_migrations, stats.e_to_p_migrations);
        return result;
    }

    // Default: clean profile, no action needed
    result.explanation = fmt::format(
        "Thread has {} migration(s) with no cross-type activity and "
        "no high-frequency concern. No action required.",
        stats.total_migrations);
    return result;
}

}  // namespace threveal::analysis

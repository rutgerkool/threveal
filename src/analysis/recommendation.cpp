/**
 *  @file       recommendation.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Implementation of the rule-based thread affinity recommendation engine.
 */

#include "threveal/analysis/recommendation.hpp"

#include "threveal/analysis/migration_analyzer.hpp"

#include <cstdint>
#include <fmt/format.h>
#include <optional>
#include <utility>
#include <vector>

namespace threveal::analysis
{

namespace
{

constexpr double kNsPerSec = 1.0e9;

/**
 *  Returns count as a fraction of total, or 0.0 when total is zero.
 */
constexpr auto fractionOf(std::uint32_t count, std::uint32_t total) noexcept -> double
{
    return total > 0 ? static_cast<double>(count) / static_cast<double>(total) : 0.0;
}

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
    double rate = computeMigrationRate(stats.total_migrations);
    double p_to_e_fraction = fractionOf(stats.p_to_e_migrations, stats.total_migrations);

    auto outcome = evaluateRules(stats, rate, p_to_e_fraction);

    return ThreadRecommendation{
        .tid = stats.tid,
        .pid = stats.pid,
        .comm = stats.comm,
        .recommendation = outcome.recommendation,
        .explanation = std::move(outcome.explanation),
        .migration_rate_per_second = rate,
        .p_to_e_fraction = p_to_e_fraction,
    };
}

auto RecommendationEngine::evaluateRules(const ThreadStatistics& stats, double rate,
                                         double p_to_e_fraction) const -> RuleOutcome
{
    if (stats.total_migrations < min_migrations_)
    {
        return RuleOutcome{
            .recommendation = AffinityRecommendation::kNone,
            .explanation = "Insufficient migration data for analysis.",
        };
    }

    // The first rule that matches decides, so the order below is the rule priority
    if (auto outcome = pinToPCoresRule(stats, p_to_e_fraction))
    {
        return std::move(*outcome);
    }
    if (auto outcome = reduceMigrationsRule(rate))
    {
        return std::move(*outcome);
    }
    if (auto outcome = pinToECoresRule(stats))
    {
        return std::move(*outcome);
    }
    if (auto outcome = inconclusiveRule(stats))
    {
        return std::move(*outcome);
    }

    return noActionOutcome(stats);
}

auto RecommendationEngine::pinToPCoresRule(const ThreadStatistics& stats,
                                           double p_to_e_fraction) const
    -> std::optional<RuleOutcome>
{
    bool high_p_to_e_fraction = (p_to_e_fraction >= p_to_e_fraction_threshold_);
    bool significant_ipc_loss = (stats.avg_ipc_loss_on_p_to_e < ipc_loss_threshold_);

    if (!high_p_to_e_fraction || !significant_ipc_loss)
    {
        return std::nullopt;
    }

    return RuleOutcome{
        .recommendation = AffinityRecommendation::kPinToPCores,
        .explanation =
            fmt::format("Thread experiences an average IPC loss of {:.2f} on P→E migrations "
                        "({:.0f}% of all migrations). "
                        "Pinning to P-cores should eliminate this migration penalty.",
                        stats.avg_ipc_loss_on_p_to_e, p_to_e_fraction * 100.0),
    };
}

auto RecommendationEngine::reduceMigrationsRule(double rate) const -> std::optional<RuleOutcome>
{
    if (rate < high_migration_rate_)
    {
        return std::nullopt;
    }

    return RuleOutcome{
        .recommendation = AffinityRecommendation::kReduceMigrations,
        .explanation =
            fmt::format("Thread is migrating at {:.0f} migrations/second. "
                        "Excessive migration frequency causes repeated cache-state destruction. "
                        "Pinning to a fixed core set should reduce this overhead.",
                        rate),
    };
}

auto RecommendationEngine::pinToECoresRule(const ThreadStatistics& stats) const
    -> std::optional<RuleOutcome>
{
    // Share of migrations that land on an E-core
    double e_core_fraction =
        fractionOf(stats.p_to_e_migrations + stats.e_to_e_migrations, stats.total_migrations);

    bool mostly_on_e_cores = (e_core_fraction > e_core_majority_threshold_);
    bool no_significant_p_core_benefit = (stats.avg_ipc_gain_on_e_to_p < significant_ipc_gain_);

    // Only worth pinning if the scheduler actually moves the thread to P-cores
    bool is_being_pulled_to_p_cores = (stats.e_to_p_migrations > 0);

    if (!mostly_on_e_cores || !no_significant_p_core_benefit || !is_being_pulled_to_p_cores)
    {
        return std::nullopt;
    }

    return RuleOutcome{
        .recommendation = AffinityRecommendation::kPinToECores,
        .explanation = fmt::format(
            "Thread has {:.0f}% E-core activity and gains only {:.2f} IPC "
            "when migrated to a P-core. "
            "Pinning to E-cores eliminates unnecessary migrations without harming throughput.",
            e_core_fraction * 100.0, stats.avg_ipc_gain_on_e_to_p),
    };
}

auto RecommendationEngine::inconclusiveRule(const ThreadStatistics& stats)
    -> std::optional<RuleOutcome>
{
    std::uint32_t cross_type = stats.p_to_e_migrations + stats.e_to_p_migrations;
    if (cross_type == 0)
    {
        return std::nullopt;
    }

    return RuleOutcome{
        .recommendation = AffinityRecommendation::kInvestigateFurther,
        .explanation = fmt::format("Thread has {} cross-type migration(s) (P→E: {}, E→P: {}) "
                                   "but patterns are inconclusive. "
                                   "Manual inspection of the profiling data is recommended.",
                                   cross_type, stats.p_to_e_migrations, stats.e_to_p_migrations),
    };
}

auto RecommendationEngine::noActionOutcome(const ThreadStatistics& stats) -> RuleOutcome
{
    return RuleOutcome{
        .recommendation = AffinityRecommendation::kNone,
        .explanation = fmt::format("Thread has {} migration(s) with no cross-type activity and "
                                   "no high-frequency concern. No action required.",
                                   stats.total_migrations),
    };
}

}  // namespace threveal::analysis

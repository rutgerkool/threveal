/**
 *  @file       recommendation.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Thread affinity recommendations for hybrid-core migration analysis.
 */

#ifndef THREVEAL_ANALYSIS_RECOMMENDATION_HPP_
#define THREVEAL_ANALYSIS_RECOMMENDATION_HPP_

#include "threveal/analysis/migration_analyzer.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace threveal::analysis
{

/**
 *  Actionable thread affinity recommendations produced by the recommendation engine.
 */
enum class AffinityRecommendation : std::uint8_t
{
    /**
     *  No action needed; migration activity is below concern thresholds.
     */
    kNone = 0,

    /**
     *  Thread repeatedly migrates P to E with measurable IPC loss.
     */
    kPinToPCores = 1,

    /**
     *  Thread predominantly runs on E-cores without benefiting from P-cores.
     */
    kPinToECores = 2,

    /**
     *  Migration frequency is high enough to cause excessive cache overhead.
     */
    kReduceMigrations = 3,

    /**
     *  Cross-type migration activity exists but patterns are ambiguous.
     */
    kInvestigateFurther = 4,
};

/**
 *  Converts an AffinityRecommendation to a short human-readable label.
 *
 *  @param      rec  The recommendation to convert.
 *  @return     A string view suitable for terminal or JSON output.
 */
[[nodiscard]] constexpr auto toString(AffinityRecommendation rec) noexcept -> std::string_view
{
    switch (rec)
    {
        case AffinityRecommendation::kNone:
            return "None";
        case AffinityRecommendation::kPinToPCores:
            return "PinToPCores";
        case AffinityRecommendation::kPinToECores:
            return "PinToECores";
        case AffinityRecommendation::kReduceMigrations:
            return "ReduceMigrations";
        case AffinityRecommendation::kInvestigateFurther:
            return "InvestigateFurther";
    }
    return "Invalid";
}

/**
 *  Recommendation result for a single thread.
 */
struct ThreadRecommendation
{
    /**
     *  Thread ID this recommendation applies to.
     */
    std::uint32_t tid;

    /**
     *  Process ID of the thread's owning process.
     */
    std::uint32_t pid;

    /**
     *  Command name of the thread.
     */
    std::string comm;

    /**
     *  The recommended action for this thread.
     */
    AffinityRecommendation recommendation;

    /**
     *  Human-readable explanation of why this recommendation was made.
     */
    std::string explanation;

    /**
     *  Observed migration rate in migrations per second.
     */
    double migration_rate_per_second;

    /**
     *  Fraction of total migrations that are P to E (0.0 to 1.0).
     */
    double p_to_e_fraction;
};

/**
 *  Generates per-thread affinity recommendations from migration statistics.
 */
class RecommendationEngine
{
  public:
    /**
     *  Minimum number of migrations required before generating a recommendation.
     */
    static constexpr std::uint32_t kDefaultMinMigrations = 5;

    /**
     *  IPC loss threshold (negative) for recommending P-core pinning.
     */
    static constexpr double kDefaultIpcLossThreshold = -0.10;

    /**
     *  P→E fraction required to trigger the P-core pinning rule.
     */
    static constexpr double kDefaultPToEFractionThreshold = 0.30;

    /**
     *  Migration rate (migrations/second) above which reduction is recommended.
     */
    static constexpr double kDefaultHighMigrationRate = 100.0;

    /**
     *  E-core occupancy fraction above which E-core pinning is considered.
     */
    static constexpr double kDefaultECoreMajorityThreshold = 0.50;

    /**
     *  Minimum IPC gain from E→P that would indicate a thread benefits from P-cores.
     */
    static constexpr double kDefaultSignificantIpcGain = 0.10;

    /**
     *  Constructs the engine for a specific profiling window.
     *
     *  @param      profiling_duration_ns  Total profiling window in nanoseconds.
     */
    explicit RecommendationEngine(std::uint64_t profiling_duration_ns) noexcept;

    /**
     *  Generates per-thread recommendations from a set of thread statistics.
     *
     *  @param      thread_stats  Per-thread statistics from MigrationAnalyzer::analyze().
     *  @return     One recommendation per thread.
     */
    [[nodiscard]] auto analyze(const std::vector<ThreadStatistics>& thread_stats) const
        -> std::vector<ThreadRecommendation>;

    /**
     *  Sets the minimum number of migrations required for analysis.
     *
     *  @param      min  New minimum migration count.
     */
    void setMinMigrations(std::uint32_t min) noexcept;

    /**
     *  Sets the IPC loss threshold for recommending P-core pinning.
     *
     *  @param      threshold  Negative IPC delta below which a P→E migration
     *                          is considered harmful (e.g. −0.10).
     */
    void setIpcLossThreshold(double threshold) noexcept;

    /**
     *  Sets the migration rate threshold above which reduction is recommended.
     *
     *  @param      rate_per_sec  Migrations per second (e.g. 100.0).
     */
    void setHighMigrationRate(double rate_per_sec) noexcept;

  private:
    /**
     *  Generates a recommendation for a single thread.
     *
     *  @param      stats  Statistics for the thread to analyse.
     *  @return     The recommendation with explanation and supporting metrics.
     */
    [[nodiscard]] auto recommend(const ThreadStatistics& stats) const -> ThreadRecommendation;

    /**
     *  Computes the migration rate in migrations per second.
     *
     *  @param      total_migrations  Number of observed migrations.
     *  @return     Migrations per second, or 0.0 if the profiling duration is zero.
     */
    [[nodiscard]] auto computeMigrationRate(std::uint32_t total_migrations) const noexcept
        -> double;

    std::uint64_t profiling_duration_ns_;
    std::uint32_t min_migrations_{kDefaultMinMigrations};
    double ipc_loss_threshold_{kDefaultIpcLossThreshold};
    double p_to_e_fraction_threshold_{kDefaultPToEFractionThreshold};
    double high_migration_rate_{kDefaultHighMigrationRate};
    double e_core_majority_threshold_{kDefaultECoreMajorityThreshold};
    double significant_ipc_gain_{kDefaultSignificantIpcGain};
};

}  // namespace threveal::analysis

#endif  // THREVEAL_ANALYSIS_RECOMMENDATION_HPP_

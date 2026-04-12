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
    static constexpr std::uint32_t kDefaultMinMigrations = 5;
    static constexpr double kDefaultIpcLossThreshold = -0.10;
    static constexpr double kDefaultPToEFractionThreshold = 0.30;
    static constexpr double kDefaultHighMigrationRate = 100.0;
    static constexpr double kDefaultECoreMajorityThreshold = 0.50;
    static constexpr double kDefaultSignificantIpcGain = 0.10;

    explicit RecommendationEngine(std::uint64_t profiling_duration_ns) noexcept;

    [[nodiscard]] auto analyze(const std::vector<ThreadStatistics>& thread_stats) const
        -> std::vector<ThreadRecommendation>;

    void setMinMigrations(std::uint32_t min) noexcept;
    void setIpcLossThreshold(double threshold) noexcept;
    void setHighMigrationRate(double rate_per_sec) noexcept;

  private:
    [[nodiscard]] auto recommend(const ThreadStatistics& stats) const -> ThreadRecommendation;
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

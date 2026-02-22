/**
 *  @file       recommendation.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Thread affinity recommendations for hybrid-core migration analysis.
 */

#ifndef THREVEAL_ANALYSIS_RECOMMENDATION_HPP_
#define THREVEAL_ANALYSIS_RECOMMENDATION_HPP_

#include <cstdint>
#include <string_view>

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
     *  Thread repeatedly migrates P→E with measurable IPC loss.
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

}  // namespace threveal::analysis

#endif  // THREVEAL_ANALYSIS_RECOMMENDATION_HPP_

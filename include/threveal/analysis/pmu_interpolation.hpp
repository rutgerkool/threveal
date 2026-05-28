/**
 *  @file       pmu_interpolation.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Interpolation of PMU counter values between samples.
 */

#ifndef THREVEAL_ANALYSIS_PMU_INTERPOLATION_HPP_
#define THREVEAL_ANALYSIS_PMU_INTERPOLATION_HPP_

#include "threveal/core/events.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace threveal::analysis
{

/**
 *  Running counter totals of one thread at a point in time.
 */
struct CounterTotals
{
    double instructions = 0.0;
    double cycles = 0.0;
    double llc_misses = 0.0;
    double llc_references = 0.0;
    double branch_misses = 0.0;
};

/**
 *  Interpolates a thread's running counter totals at a point in time.
 *
 *  @param      samples  The thread's samples, sorted by timestamp.
 *  @param      time_ns  The time to interpolate at.
 *  @return     The totals at time_ns, or std::nullopt if time_ns is outside the
 *              sampled range.
 */
[[nodiscard]] auto interpolateTotals(std::span<const core::PmuSample> samples,
                                     std::uint64_t time_ns) -> std::optional<CounterTotals>;

}  // namespace threveal::analysis

#endif  // THREVEAL_ANALYSIS_PMU_INTERPOLATION_HPP_

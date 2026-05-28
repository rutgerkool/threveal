/**
 *  @file       pmu_interpolation.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Implementation of PMU counter interpolation.
 */

#include "threveal/analysis/pmu_interpolation.hpp"

#include "threveal/core/events.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace threveal::analysis
{

namespace
{

/**
 *  Adds a fraction of a sample's counts to the running totals.
 */
void addScaled(CounterTotals& totals, const core::PmuSample& sample, double fraction) noexcept
{
    totals.instructions += fraction * static_cast<double>(sample.instructions);
    totals.cycles += fraction * static_cast<double>(sample.cycles);
    totals.llc_misses += fraction * static_cast<double>(sample.llc_misses);
    totals.llc_references += fraction * static_cast<double>(sample.llc_references);
    totals.branch_misses += fraction * static_cast<double>(sample.branch_misses);
}

}  // namespace

auto interpolateTotals(std::span<const core::PmuSample> samples, std::uint64_t time_ns)
    -> std::optional<CounterTotals>
{
    if (samples.empty() || time_ns < samples.front().timestamp_ns ||
        time_ns > samples.back().timestamp_ns)
    {
        return std::nullopt;
    }

    CounterTotals totals{};
    addScaled(totals, samples.front(), 1.0);
    std::uint64_t previous_ns = samples.front().timestamp_ns;

    for (const auto& sample : samples.subspan(1))
    {
        if (time_ns < sample.timestamp_ns)
        {
            auto elapsed = static_cast<double>(time_ns - previous_ns);
            auto length = static_cast<double>(sample.timestamp_ns - previous_ns);
            addScaled(totals, sample, elapsed / length);
            return totals;
        }

        addScaled(totals, sample, 1.0);
        previous_ns = sample.timestamp_ns;
    }

    return totals;
}

}  // namespace threveal::analysis

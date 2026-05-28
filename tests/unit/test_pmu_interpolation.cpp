/**
 *  @file       test_pmu_interpolation.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Unit tests for PMU counter interpolation.
 */

#include "threveal/analysis/pmu_interpolation.hpp"
#include "threveal/core/events.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

using Catch::Approx;
using threveal::analysis::interpolateTotals;
using threveal::core::PmuSample;

namespace
{

/**
 *  Builds a sample whose counters are all distinct multiples of base.
 */
auto makeSample(std::uint64_t timestamp_ns, std::uint64_t base) -> PmuSample
{
    return PmuSample{
        .timestamp_ns = timestamp_ns,
        .tid = 42,
        .cpu_id = 0,
        .instructions = base * 1,
        .cycles = base * 2,
        .llc_misses = base * 3,
        .llc_references = base * 4,
        .branch_misses = base * 5,
    };
}

const std::vector<PmuSample> kSamples = {
    makeSample(1'000, 100),
    makeSample(2'000, 200),
    makeSample(3'000, 400),
};

}  // namespace

TEST_CASE("interpolateTotals returns nullopt without samples", "[analysis][pmu_interpolation]")
{
    REQUIRE_FALSE(interpolateTotals({}, 1'000).has_value());
}

TEST_CASE("interpolateTotals returns nullopt outside the sampled range",
          "[analysis][pmu_interpolation]")
{
    REQUIRE_FALSE(interpolateTotals(kSamples, 999).has_value());
    REQUIRE_FALSE(interpolateTotals(kSamples, 3'001).has_value());
}

TEST_CASE("interpolateTotals returns the running total at a sample",
          "[analysis][pmu_interpolation]")
{
    SECTION("first sample")
    {
        auto totals = interpolateTotals(kSamples, 1'000);
        REQUIRE(totals.has_value());
        REQUIRE(totals->instructions == Approx(100));
    }

    SECTION("last sample")
    {
        auto totals = interpolateTotals(kSamples, 3'000);
        REQUIRE(totals.has_value());
        REQUIRE(totals->instructions == Approx(700));
    }
}

TEST_CASE("interpolateTotals interpolates every counter linearly within an interval",
          "[analysis][pmu_interpolation]")
{
    auto totals = interpolateTotals(kSamples, 2'250);
    REQUIRE(totals.has_value());

    REQUIRE(totals->instructions == Approx(400));
    REQUIRE(totals->cycles == Approx(800));
    REQUIRE(totals->llc_misses == Approx(1200));
    REQUIRE(totals->llc_references == Approx(1600));
    REQUIRE(totals->branch_misses == Approx(2000));
}

TEST_CASE("interpolateTotals handles samples with the same timestamp",
          "[analysis][pmu_interpolation]")
{
    std::vector<PmuSample> samples = {
        makeSample(1'000, 10),
        makeSample(1'000, 20),
        makeSample(2'000, 40),
    };

    auto totals = interpolateTotals(samples, 1'000);
    REQUIRE(totals.has_value());
    REQUIRE(totals->instructions == Approx(30));
}

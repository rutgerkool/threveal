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
using threveal::analysis::CounterTotals;
using threveal::analysis::interpolateTotals;
using threveal::analysis::ratesBetween;
using threveal::analysis::ratesOver;
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

/**
 *  Builds a sample with only instructions and cycles, for testing IPC.
 */
auto makeIpcSample(std::uint64_t timestamp_ns, std::uint64_t instructions, std::uint64_t cycles)
    -> PmuSample
{
    return PmuSample{
        .timestamp_ns = timestamp_ns,
        .tid = 42,
        .cpu_id = 0,
        .instructions = instructions,
        .cycles = cycles,
        .llc_misses = 0,
        .llc_references = 0,
        .branch_misses = 0,
    };
}

/**
 *  An interval with IPC 2.0 followed by an interval with IPC 1.0.
 */
const std::vector<PmuSample> kIpcSamples = {
    makeIpcSample(1'000, 0, 0),
    makeIpcSample(2'000, 2'000, 1'000),
    makeIpcSample(3'000, 1'000, 1'000),
};

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

TEST_CASE("ratesBetween computes every rate over the window", "[analysis][pmu_interpolation]")
{
    CounterTotals from{
        .instructions = 1000,
        .cycles = 1000,
        .llc_misses = 10,
        .llc_references = 100,
        .branch_misses = 5,
    };
    CounterTotals to{
        .instructions = 3000,
        .cycles = 2000,
        .llc_misses = 40,
        .llc_references = 200,
        .branch_misses = 25,
    };

    auto rates = ratesBetween(from, to);

    REQUIRE(rates.ipc == Approx(2.0));
    REQUIRE(rates.llc_miss_rate == Approx(0.3));
    REQUIRE(rates.branch_miss_rate == Approx(0.01));
}

TEST_CASE("ratesBetween returns zero rates for an empty window", "[analysis][pmu_interpolation]")
{
    CounterTotals totals{
        .instructions = 1000,
        .cycles = 1000,
        .llc_misses = 10,
        .llc_references = 100,
        .branch_misses = 5,
    };

    auto rates = ratesBetween(totals, totals);

    REQUIRE(rates.ipc == 0.0);
    REQUIRE(rates.llc_miss_rate == 0.0);
    REQUIRE(rates.branch_miss_rate == 0.0);
}

TEST_CASE("ratesBetween guards each denominator independently", "[analysis][pmu_interpolation]")
{
    CounterTotals from{};
    CounterTotals to{
        .instructions = 1000,
        .cycles = 0,
        .llc_misses = 0,
        .llc_references = 0,
        .branch_misses = 10,
    };

    auto rates = ratesBetween(from, to);

    REQUIRE(rates.ipc == 0.0);
    REQUIRE(rates.llc_miss_rate == 0.0);
    REQUIRE(rates.branch_miss_rate == Approx(0.01));
}

TEST_CASE("ratesOver measures a window between samples", "[analysis][pmu_interpolation]")
{
    SECTION("exactly one interval")
    {
        auto rates = ratesOver(kIpcSamples, 1'000, 2'000);
        REQUIRE(rates.has_value());
        REQUIRE(rates->ipc == Approx(2.0));
    }

    SECTION("half of each interval")
    {
        auto rates = ratesOver(kIpcSamples, 1'500, 2'500);
        REQUIRE(rates.has_value());
        REQUIRE(rates->ipc == Approx(1.5));
    }
}

TEST_CASE("ratesOver clamps the window to the sampled range", "[analysis][pmu_interpolation]")
{
    SECTION("window starts before the first sample")
    {
        auto rates = ratesOver(kIpcSamples, 0, 2'000);
        REQUIRE(rates.has_value());
        REQUIRE(rates->ipc == Approx(2.0));
    }

    SECTION("window ends after the last sample")
    {
        auto rates = ratesOver(kIpcSamples, 2'000, 9'000);
        REQUIRE(rates.has_value());
        REQUIRE(rates->ipc == Approx(1.0));
    }
}

TEST_CASE("ratesOver returns nullopt without overlap", "[analysis][pmu_interpolation]")
{
    REQUIRE_FALSE(ratesOver({}, 0, 1'000).has_value());
    REQUIRE_FALSE(ratesOver(kIpcSamples, 0, 1'000).has_value());
    REQUIRE_FALSE(ratesOver(kIpcSamples, 3'000, 4'000).has_value());
    REQUIRE_FALSE(ratesOver(kIpcSamples, 2'000, 2'000).has_value());
}

/**
 *  @file       test_recommendation.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Unit tests for RecommendationEngine.
 */

#include "threveal/analysis/migration_analyzer.hpp"
#include "threveal/analysis/recommendation.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <vector>

using threveal::analysis::AffinityRecommendation;
using threveal::analysis::RecommendationEngine;
using threveal::analysis::ThreadStatistics;

namespace
{

/// Builds a ThreadStatistics with all fields explicitly set.
auto makeStats(std::uint32_t tid, std::uint32_t total, std::uint32_t p_to_e, std::uint32_t e_to_p,
               std::uint32_t p_to_p, std::uint32_t e_to_e, double avg_ipc_loss_p_to_e = 0.0,
               double avg_ipc_gain_e_to_p = 0.0, double avg_cache_delta = 0.0) -> ThreadStatistics
{
    ThreadStatistics s{};
    s.tid = tid;
    s.pid = tid;
    s.comm = "thread_" + std::to_string(tid);
    s.total_migrations = total;
    s.p_to_e_migrations = p_to_e;
    s.e_to_p_migrations = e_to_p;
    s.p_to_p_migrations = p_to_p;
    s.e_to_e_migrations = e_to_e;
    s.avg_ipc_loss_on_p_to_e = avg_ipc_loss_p_to_e;
    s.avg_ipc_gain_on_e_to_p = avg_ipc_gain_e_to_p;
    s.avg_cache_miss_delta = avg_cache_delta;
    return s;
}

/// One second profiling window expressed in nanoseconds.
constexpr std::uint64_t kOneSecondNs = 1'000'000'000ULL;

}  // namespace

TEST_CASE("RecommendationEngine with empty input returns empty output",
          "[analysis][RecommendationEngine]")
{
    RecommendationEngine engine(kOneSecondNs);
    auto results = engine.analyze({});
    REQUIRE(results.empty());
}

TEST_CASE("RecommendationEngine returns kNone for thread with too few migrations",
          "[analysis][RecommendationEngine]")
{
    RecommendationEngine engine(kOneSecondNs);

    // 4 migrations < default minimum of 5
    auto stats = makeStats(42, 4, 2, 0, 2, 0, -0.5);
    auto results = engine.analyze({stats});

    REQUIRE(results.size() == 1);
    REQUIRE(results[0].recommendation == AffinityRecommendation::kNone);
    REQUIRE_FALSE(results[0].explanation.empty());
}

TEST_CASE("RecommendationEngine uses configurable minimum migrations",
          "[analysis][RecommendationEngine]")
{
    RecommendationEngine engine(kOneSecondNs);
    engine.setMinMigrations(10);

    // 8 migrations < custom minimum of 10
    auto stats = makeStats(42, 8, 5, 0, 3, 0, -0.5);
    auto results = engine.analyze({stats});

    REQUIRE(results[0].recommendation == AffinityRecommendation::kNone);
}

TEST_CASE("RecommendationEngine recommends kPinToPCores for high P→E fraction with IPC loss",
          "[analysis][RecommendationEngine]")
{
    RecommendationEngine engine(kOneSecondNs);

    // 50% P to E migrations with -0.5 IPC loss
    auto stats = makeStats(42, 10, 5, 0, 5, 0, -0.5);
    auto results = engine.analyze({stats});

    REQUIRE(results[0].recommendation == AffinityRecommendation::kPinToPCores);
    REQUIRE_FALSE(results[0].explanation.empty());
    REQUIRE(results[0].p_to_e_fraction == Catch::Approx(0.5));
}

TEST_CASE("RecommendationEngine does NOT recommend kPinToPCores when IPC loss is small",
          "[analysis][RecommendationEngine]")
{
    RecommendationEngine engine(kOneSecondNs);

    // High P to E fraction but low IPC loss
    auto stats = makeStats(42, 10, 5, 0, 5, 0, -0.05);
    auto results = engine.analyze({stats});

    REQUIRE(results[0].recommendation != AffinityRecommendation::kPinToPCores);
}

TEST_CASE("RecommendationEngine does NOT recommend kPinToPCores when P→E fraction is low",
          "[analysis][RecommendationEngine]")
{
    RecommendationEngine engine(kOneSecondNs);

    // 10% P to E migrations but high IPC loss
    auto stats = makeStats(42, 10, 1, 0, 9, 0, -0.5);
    auto results = engine.analyze({stats});

    REQUIRE(results[0].recommendation != AffinityRecommendation::kPinToPCores);
}

TEST_CASE("RecommendationEngine respects custom IPC loss threshold",
          "[analysis][RecommendationEngine]")
{
    RecommendationEngine engine(kOneSecondNs);
    engine.setIpcLossThreshold(-0.30);

    auto stats = makeStats(42, 10, 5, 0, 5, 0, -0.20);

    // -0.20 is above the -0.30 threshold, so should NOT recommend P-core pinning
    auto results = engine.analyze({stats});
    REQUIRE(results[0].recommendation != AffinityRecommendation::kPinToPCores);

    // -0.40 is below the -0.30 threshold, so should recommend P-core pinning
    stats.avg_ipc_loss_on_p_to_e = -0.40;
    results = engine.analyze({stats});
    REQUIRE(results[0].recommendation == AffinityRecommendation::kPinToPCores);
}

TEST_CASE("RecommendationEngine recommends kReduceMigrations for very high migration rate",
          "[analysis][RecommendationEngine]")
{
    // 200 migrations in 1 second > default 100 threshold
    RecommendationEngine engine(kOneSecondNs);

    // Use p_to_p only to avoid triggering rule 1
    auto stats = makeStats(42, 200, 0, 0, 200, 0);
    auto results = engine.analyze({stats});

    REQUIRE(results[0].recommendation == AffinityRecommendation::kReduceMigrations);
    REQUIRE(results[0].migration_rate_per_second == Catch::Approx(200.0));
}

TEST_CASE("RecommendationEngine respects custom high migration rate threshold",
          "[analysis][RecommendationEngine]")
{
    RecommendationEngine engine(kOneSecondNs);
    engine.setHighMigrationRate(50.0);

    // 60 migrations per second > custom 50 threshold
    auto stats = makeStats(42, 60, 0, 0, 60, 0);
    auto results = engine.analyze({stats});

    REQUIRE(results[0].recommendation == AffinityRecommendation::kReduceMigrations);
}

TEST_CASE("RecommendationEngine computes migration rate correctly",
          "[analysis][RecommendationEngine]")
{
    // 500ms window, 50 migrations → 100 migrations per second exactly on the boundary
    constexpr std::uint64_t kHalfSecondNs = 500'000'000ULL;
    RecommendationEngine engine(kHalfSecondNs);

    auto stats = makeStats(42, 50, 0, 0, 50, 0);
    auto results = engine.analyze({stats});

    // 50 / 0.5 sec = 100 migrations per second, so right at the threshold, not above
    REQUIRE(results[0].migration_rate_per_second == Catch::Approx(100.0));
}

TEST_CASE("RecommendationEngine handles zero profiling duration without crashing",
          "[analysis][RecommendationEngine]")
{
    RecommendationEngine engine(0);

    auto stats = makeStats(42, 10, 0, 0, 10, 0);
    auto results = engine.analyze({stats});

    REQUIRE(results[0].migration_rate_per_second == Catch::Approx(0.0));
    REQUIRE_FALSE(results[0].explanation.empty());
}

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

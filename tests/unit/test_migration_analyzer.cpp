/**
 *  @file       test_migration_analyzer.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Unit tests for the migration analyzer.
 */

#include "threveal/analysis/event_store.hpp"
#include "threveal/analysis/migration_analyzer.hpp"
#include "threveal/core/events.hpp"
#include "threveal/core/topology.hpp"
#include "threveal/core/types.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace threveal;

TEST_CASE("MigrationAnalyzer empty store returns empty results", "[analysis][migration_analyzer]")
{
    auto store = analysis::EventStore();
    auto topology = core::TopologyMap{};

    auto analyzer = analysis::MigrationAnalyzer(store, topology);
    auto result = analyzer.analyze();

    REQUIRE(result.total_migrations == 0);
    REQUIRE(result.correlated_migrations == 0);
    REQUIRE(result.impacts.empty());
    REQUIRE(result.type_stats.empty());
    REQUIRE(result.thread_stats.empty());
}

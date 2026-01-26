/**
 *  @file       test_event_store.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Unit tests for EventStore.
 */

#include "threveal/analysis/event_store.hpp"
#include "threveal/core/events.hpp"
#include "threveal/core/types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>

using threveal::analysis::EventStore;
using threveal::core::CpuId;
using threveal::core::MigrationEvent;
using threveal::core::PmuSample;

namespace
{

auto makeMigration(std::uint64_t timestamp_ns, std::uint32_t tid, CpuId src, CpuId dst)
    -> MigrationEvent
{
    return MigrationEvent{
        .timestamp_ns = timestamp_ns,
        .pid = tid,
        .tid = tid,
        .src_cpu = src,
        .dst_cpu = dst,
        .comm = {},
    };
}

auto makePmuSample(std::uint64_t timestamp_ns, std::uint32_t tid, CpuId cpu) -> PmuSample
{
    return PmuSample{
        .timestamp_ns = timestamp_ns,
        .tid = tid,
        .cpu_id = cpu,
        .instructions = 1000000,
        .cycles = 500000,
        .llc_misses = 100,
        .llc_references = 1000,
        .branch_misses = 50,
    };
}

}  // namespace

TEST_CASE("EventStore starts empty", "[analysis][EventStore]")
{
    EventStore store;

    REQUIRE(store.migrationCount() == 0);
    REQUIRE(store.pmuSampleCount() == 0);
    REQUIRE(store.allMigrations().empty());
    REQUIRE(store.allPmuSamples().empty());
}

TEST_CASE("EventStore stores migrations", "[analysis][EventStore]")
{
    EventStore store;

    store.addMigration(makeMigration(1000, 42, 0, 12));
    store.addMigration(makeMigration(2000, 42, 12, 0));

    REQUIRE(store.migrationCount() == 2);
    REQUIRE(store.allMigrations().size() == 2);
    REQUIRE(store.allMigrations()[0].timestamp_ns == 1000);
    REQUIRE(store.allMigrations()[1].timestamp_ns == 2000);
}

TEST_CASE("EventStore maintains migrations sorted by timestamp", "[analysis][EventStore]")
{
    EventStore store;

    // Insert migrations out of chronological order to verify sorting
    store.addMigration(makeMigration(3000, 42, 0, 1));
    store.addMigration(makeMigration(1000, 42, 1, 0));
    store.addMigration(makeMigration(4000, 42, 0, 1));
    store.addMigration(makeMigration(2000, 42, 1, 0));

    REQUIRE(store.migrationCount() == 4);

    // Verify they are stored in ascending timestamp order
    auto all = store.allMigrations();
    REQUIRE(all[0].timestamp_ns == 1000);
    REQUIRE(all[1].timestamp_ns == 2000);
    REQUIRE(all[2].timestamp_ns == 3000);
    REQUIRE(all[3].timestamp_ns == 4000);
}

TEST_CASE("EventStore stores PMU samples", "[analysis][EventStore]")
{
    EventStore store;

    store.addPmuSample(makePmuSample(1000, 42, 0));
    store.addPmuSample(makePmuSample(2000, 42, 12));

    REQUIRE(store.pmuSampleCount() == 2);
    REQUIRE(store.allPmuSamples().size() == 2);
    REQUIRE(store.allPmuSamples()[0].timestamp_ns == 1000);
    REQUIRE(store.allPmuSamples()[1].timestamp_ns == 2000);
}

TEST_CASE("EventStore maintains PMU samples sorted by timestamp", "[analysis][EventStore]")
{
    EventStore store;

    // Insert samples out of chronological order to verify sorting
    store.addPmuSample(makePmuSample(3000, 42, 0));
    store.addPmuSample(makePmuSample(1000, 42, 0));
    store.addPmuSample(makePmuSample(4000, 42, 0));
    store.addPmuSample(makePmuSample(2000, 42, 0));

    REQUIRE(store.pmuSampleCount() == 4);

    // Verify they are stored in ascending timestamp order
    auto all = store.allPmuSamples();
    REQUIRE(all[0].timestamp_ns == 1000);
    REQUIRE(all[1].timestamp_ns == 2000);
    REQUIRE(all[2].timestamp_ns == 3000);
    REQUIRE(all[3].timestamp_ns == 4000);
}

TEST_CASE("EventStore filters migrations by thread", "[analysis][EventStore]")
{
    EventStore store;

    store.addMigration(makeMigration(1000, 42, 0, 1));
    store.addMigration(makeMigration(2000, 43, 0, 1));
    store.addMigration(makeMigration(3000, 42, 1, 0));
    store.addMigration(makeMigration(4000, 44, 0, 1));

    auto thread42 = store.migrationsForThread(42);
    REQUIRE(thread42.size() == 2);
    REQUIRE(thread42[0].timestamp_ns == 1000);
    REQUIRE(thread42[1].timestamp_ns == 3000);

    auto thread43 = store.migrationsForThread(43);
    REQUIRE(thread43.size() == 1);
    REQUIRE(thread43[0].timestamp_ns == 2000);

    auto thread99 = store.migrationsForThread(99);
    REQUIRE(thread99.empty());
}

TEST_CASE("EventStore filters migrations by time range", "[analysis][EventStore]")
{
    EventStore store;

    store.addMigration(makeMigration(1000, 42, 0, 1));
    store.addMigration(makeMigration(2000, 42, 1, 0));
    store.addMigration(makeMigration(3000, 42, 0, 1));
    store.addMigration(makeMigration(4000, 42, 1, 0));

    SECTION("middle range")
    {
        auto result = store.migrationsInRange(1500, 3500);
        REQUIRE(result.size() == 2);
        REQUIRE(result[0].timestamp_ns == 2000);
        REQUIRE(result[1].timestamp_ns == 3000);
    }

    SECTION("exact boundaries inclusive")
    {
        auto result = store.migrationsInRange(2000, 3000);
        REQUIRE(result.size() == 2);
        REQUIRE(result[0].timestamp_ns == 2000);
        REQUIRE(result[1].timestamp_ns == 3000);
    }

    SECTION("range before all events")
    {
        auto result = store.migrationsInRange(0, 500);
        REQUIRE(result.empty());
    }

    SECTION("range after all events")
    {
        auto result = store.migrationsInRange(5000, 6000);
        REQUIRE(result.empty());
    }

    SECTION("full range")
    {
        auto result = store.migrationsInRange(0, 10000);
        REQUIRE(result.size() == 4);
    }
}

TEST_CASE("EventStore time range query uses binary search efficiently", "[analysis][EventStore]")
{
    EventStore store;

    // Insert migrations out of order to ensure sorting works
    store.addMigration(makeMigration(5000, 42, 0, 1));
    store.addMigration(makeMigration(1000, 42, 0, 1));
    store.addMigration(makeMigration(3000, 42, 0, 1));
    store.addMigration(makeMigration(7000, 42, 0, 1));
    store.addMigration(makeMigration(9000, 42, 0, 1));

    SECTION("range query returns correct results regardless of insertion order")
    {
        // Query middle range - should find 3000, 5000, 7000
        auto result = store.migrationsInRange(2500, 7500);
        REQUIRE(result.size() == 3);
        REQUIRE(result[0].timestamp_ns == 3000);
        REQUIRE(result[1].timestamp_ns == 5000);
        REQUIRE(result[2].timestamp_ns == 7000);
    }

    SECTION("single element range")
    {
        auto result = store.migrationsInRange(3000, 3000);
        REQUIRE(result.size() == 1);
        REQUIRE(result[0].timestamp_ns == 3000);
    }
}

TEST_CASE("EventStore filters PMU samples by thread", "[analysis][EventStore]")
{
    EventStore store;

    store.addPmuSample(makePmuSample(1000, 42, 0));
    store.addPmuSample(makePmuSample(2000, 43, 0));
    store.addPmuSample(makePmuSample(3000, 42, 1));

    auto thread42 = store.pmuSamplesForThread(42);
    REQUIRE(thread42.size() == 2);
    REQUIRE(thread42[0].timestamp_ns == 1000);
    REQUIRE(thread42[1].timestamp_ns == 3000);

    auto thread99 = store.pmuSamplesForThread(99);
    REQUIRE(thread99.empty());
}

TEST_CASE("EventStore finds PMU sample before migration", "[analysis][EventStore]")
{
    EventStore store;

    store.addPmuSample(makePmuSample(1000, 42, 0));
    store.addPmuSample(makePmuSample(2000, 42, 0));
    store.addPmuSample(makePmuSample(4000, 42, 1));

    auto migration = makeMigration(3000, 42, 0, 1);

    SECTION("finds closest sample before")
    {
        auto result = store.pmuBeforeMigration(migration);
        REQUIRE(result.has_value());
        REQUIRE(result->timestamp_ns == 2000);
    }

    SECTION("returns nullopt when no sample before")
    {
        auto early_migration = makeMigration(500, 42, 0, 1);
        auto result = store.pmuBeforeMigration(early_migration);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("returns nullopt for different thread")
    {
        auto other_thread_migration = makeMigration(3000, 99, 0, 1);
        auto result = store.pmuBeforeMigration(other_thread_migration);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("includes sample at exact migration time")
    {
        auto exact_migration = makeMigration(2000, 42, 0, 1);
        auto result = store.pmuBeforeMigration(exact_migration);
        REQUIRE(result.has_value());
        REQUIRE(result->timestamp_ns == 2000);
    }
}

TEST_CASE("EventStore finds PMU sample after migration", "[analysis][EventStore]")
{
    EventStore store;

    store.addPmuSample(makePmuSample(1000, 42, 0));
    store.addPmuSample(makePmuSample(3000, 42, 1));
    store.addPmuSample(makePmuSample(4000, 42, 1));

    auto migration = makeMigration(2000, 42, 0, 1);

    SECTION("finds closest sample after")
    {
        auto result = store.pmuAfterMigration(migration);
        REQUIRE(result.has_value());
        REQUIRE(result->timestamp_ns == 3000);
    }

    SECTION("returns nullopt when no sample after")
    {
        auto late_migration = makeMigration(5000, 42, 1, 0);
        auto result = store.pmuAfterMigration(late_migration);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("returns nullopt for different thread")
    {
        auto other_thread_migration = makeMigration(2000, 99, 0, 1);
        auto result = store.pmuAfterMigration(other_thread_migration);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("includes sample at exact migration time")
    {
        auto exact_migration = makeMigration(3000, 42, 0, 1);
        auto result = store.pmuAfterMigration(exact_migration);
        REQUIRE(result.has_value());
        REQUIRE(result->timestamp_ns == 3000);
    }
}

TEST_CASE("EventStore PMU correlation with out-of-order insertion", "[analysis][EventStore]")
{
    EventStore store;

    // Insert samples out of order to verify binary search works correctly
    store.addPmuSample(makePmuSample(4000, 42, 1));
    store.addPmuSample(makePmuSample(1000, 42, 0));
    store.addPmuSample(makePmuSample(3000, 42, 0));
    store.addPmuSample(makePmuSample(6000, 42, 1));

    auto migration = makeMigration(3500, 42, 0, 1);

    SECTION("finds closest sample before regardless of insertion order")
    {
        auto result = store.pmuBeforeMigration(migration);
        REQUIRE(result.has_value());
        REQUIRE(result->timestamp_ns == 3000);
    }

    SECTION("finds closest sample after regardless of insertion order")
    {
        auto result = store.pmuAfterMigration(migration);
        REQUIRE(result.has_value());
        REQUIRE(result->timestamp_ns == 4000);
    }
}

TEST_CASE("EventStore PMU correlation with multiple threads", "[analysis][EventStore]")
{
    EventStore store;

    // Interleaved samples from different threads
    store.addPmuSample(makePmuSample(1000, 42, 0));
    store.addPmuSample(makePmuSample(1500, 43, 0));
    store.addPmuSample(makePmuSample(2000, 42, 0));
    store.addPmuSample(makePmuSample(2500, 43, 0));
    store.addPmuSample(makePmuSample(3000, 42, 1));
    store.addPmuSample(makePmuSample(3500, 43, 1));

    SECTION("pmuBeforeMigration finds correct thread's sample")
    {
        // Migration at 2800 for thread 42 - should find sample at 2000, not 2500 (thread 43)
        auto migration = makeMigration(2800, 42, 0, 1);
        auto result = store.pmuBeforeMigration(migration);
        REQUIRE(result.has_value());
        REQUIRE(result->timestamp_ns == 2000);
        REQUIRE(result->tid == 42);
    }

    SECTION("pmuAfterMigration finds correct thread's sample")
    {
        // Migration at 2200 for thread 42 - should find sample at 3000, not 2500 (thread 43)
        auto migration = makeMigration(2200, 42, 0, 1);
        auto result = store.pmuAfterMigration(migration);
        REQUIRE(result.has_value());
        REQUIRE(result->timestamp_ns == 3000);
        REQUIRE(result->tid == 42);
    }
}

TEST_CASE("EventStore PMU correlation with empty store", "[analysis][EventStore]")
{
    EventStore store;

    auto migration = makeMigration(1000, 42, 0, 1);

    SECTION("pmuBeforeMigration returns nullopt on empty store")
    {
        auto result = store.pmuBeforeMigration(migration);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("pmuAfterMigration returns nullopt on empty store")
    {
        auto result = store.pmuAfterMigration(migration);
        REQUIRE_FALSE(result.has_value());
    }
}

TEST_CASE("EventStore clear removes all events", "[analysis][EventStore]")
{
    EventStore store;

    store.addMigration(makeMigration(1000, 42, 0, 1));
    store.addMigration(makeMigration(2000, 42, 1, 0));
    store.addPmuSample(makePmuSample(1500, 42, 0));

    REQUIRE(store.migrationCount() == 2);
    REQUIRE(store.pmuSampleCount() == 1);

    store.clear();

    REQUIRE(store.migrationCount() == 0);
    REQUIRE(store.pmuSampleCount() == 0);
    REQUIRE(store.allMigrations().empty());
    REQUIRE(store.allPmuSamples().empty());
}

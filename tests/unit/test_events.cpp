/**
 *  @file       test_events.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Unit tests for event data structures.
 */

#include "threveal/core/events.hpp"
#include "threveal/core/topology.hpp"
#include "threveal/core/types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vector>

using Catch::Matchers::WithinRel;
using threveal::core::classifyMigration;
using threveal::core::CpuId;
using threveal::core::MigrationEvent;
using threveal::core::MigrationType;
using threveal::core::PmuSample;
using threveal::core::TopologyMap;
using threveal::core::toString;

TEST_CASE("MigrationType toString", "[events][MigrationType]")
{
    REQUIRE(toString(MigrationType::kPToP) == "P→P");
    REQUIRE(toString(MigrationType::kPToE) == "P→E");
    REQUIRE(toString(MigrationType::kEToP) == "E→P");
    REQUIRE(toString(MigrationType::kEToE) == "E→E");
    REQUIRE(toString(MigrationType::kUnknown) == "Unknown");
}

TEST_CASE("PmuSample IPC calculation", "[events][PmuSample]")
{
    SECTION("normal IPC calculation")
    {
        PmuSample sample{
            .timestamp_ns = 1000,
            .tid = 42,
            .cpu_id = 0,
            .instructions = 2000000,
            .cycles = 1000000,
            .llc_misses = 0,
            .llc_references = 0,
            .branch_misses = 0,
        };

        REQUIRE_THAT(sample.ipc(), WithinRel(2.0, 0.001));
    }

    SECTION("zero cycles returns zero IPC")
    {
        PmuSample sample{
            .timestamp_ns = 1000,
            .tid = 42,
            .cpu_id = 0,
            .instructions = 1000,
            .cycles = 0,
            .llc_misses = 0,
            .llc_references = 0,
            .branch_misses = 0,
        };

        REQUIRE(sample.ipc() == 0.0);
    }

    SECTION("fractional IPC")
    {
        PmuSample sample{
            .timestamp_ns = 1000,
            .tid = 42,
            .cpu_id = 0,
            .instructions = 500000,
            .cycles = 1000000,
            .llc_misses = 0,
            .llc_references = 0,
            .branch_misses = 0,
        };

        REQUIRE_THAT(sample.ipc(), WithinRel(0.5, 0.001));
    }
}

TEST_CASE("PmuSample LLC miss rate calculation", "[events][PmuSample]")
{
    SECTION("normal miss rate calculation")
    {
        PmuSample sample{
            .timestamp_ns = 1000,
            .tid = 42,
            .cpu_id = 0,
            .instructions = 0,
            .cycles = 0,
            .llc_misses = 100,
            .llc_references = 1000,
            .branch_misses = 0,
        };

        REQUIRE_THAT(sample.llcMissRate(), WithinRel(0.1, 0.001));
    }

    SECTION("zero references returns zero miss rate")
    {
        PmuSample sample{
            .timestamp_ns = 1000,
            .tid = 42,
            .cpu_id = 0,
            .instructions = 0,
            .cycles = 0,
            .llc_misses = 100,
            .llc_references = 0,
            .branch_misses = 0,
        };

        REQUIRE(sample.llcMissRate() == 0.0);
    }

    SECTION("100% miss rate")
    {
        PmuSample sample{
            .timestamp_ns = 1000,
            .tid = 42,
            .cpu_id = 0,
            .instructions = 0,
            .cycles = 0,
            .llc_misses = 500,
            .llc_references = 500,
            .branch_misses = 0,
        };

        REQUIRE_THAT(sample.llcMissRate(), WithinRel(1.0, 0.001));
    }
}

TEST_CASE("classifyMigration with hybrid topology", "[events][classifyMigration]")
{
    // Setup: P-cores 0-3, E-cores 4-7
    std::vector<CpuId> p_cores = {0, 1, 2, 3};
    std::vector<CpuId> e_cores = {4, 5, 6, 7};
    TopologyMap topology(p_cores, e_cores);

    SECTION("P-core to P-core migration")
    {
        MigrationEvent event{
            .timestamp_ns = 1000,
            .pid = 100,
            .tid = 100,
            .src_cpu = 0,
            .dst_cpu = 2,
            .comm = {},
        };

        REQUIRE(classifyMigration(event, topology) == MigrationType::kPToP);
    }

    SECTION("P-core to E-core migration")
    {
        MigrationEvent event{
            .timestamp_ns = 1000,
            .pid = 100,
            .tid = 100,
            .src_cpu = 1,
            .dst_cpu = 5,
            .comm = {},
        };

        REQUIRE(classifyMigration(event, topology) == MigrationType::kPToE);
    }

    SECTION("E-core to P-core migration")
    {
        MigrationEvent event{
            .timestamp_ns = 1000,
            .pid = 100,
            .tid = 100,
            .src_cpu = 6,
            .dst_cpu = 3,
            .comm = {},
        };

        REQUIRE(classifyMigration(event, topology) == MigrationType::kEToP);
    }

    SECTION("E-core to E-core migration")
    {
        MigrationEvent event{
            .timestamp_ns = 1000,
            .pid = 100,
            .tid = 100,
            .src_cpu = 4,
            .dst_cpu = 7,
            .comm = {},
        };

        REQUIRE(classifyMigration(event, topology) == MigrationType::kEToE);
    }

    SECTION("invalid source CPU returns unknown")
    {
        MigrationEvent event{
            .timestamp_ns = 1000,
            .pid = 100,
            .tid = 100,
            .src_cpu = 99,
            .dst_cpu = 0,
            .comm = {},
        };

        REQUIRE(classifyMigration(event, topology) == MigrationType::kUnknown);
    }

    SECTION("invalid destination CPU returns unknown")
    {
        MigrationEvent event{
            .timestamp_ns = 1000,
            .pid = 100,
            .tid = 100,
            .src_cpu = 0,
            .dst_cpu = 99,
            .comm = {},
        };

        REQUIRE(classifyMigration(event, topology) == MigrationType::kUnknown);
    }
}

TEST_CASE("MigrationEvent commAsStringView", "[events][MigrationEvent]")
{
    SECTION("normal command name")
    {
        MigrationEvent event{
            .timestamp_ns = 1000,
            .pid = 100,
            .tid = 100,
            .src_cpu = 0,
            .dst_cpu = 1,
            .comm = {'t', 'e', 's', 't', '\0'},
        };

        REQUIRE(event.commAsStringView() == "test");
    }

    SECTION("full length command name")
    {
        MigrationEvent event{
            .timestamp_ns = 1000,
            .pid = 100,
            .tid = 100,
            .src_cpu = 0,
            .dst_cpu = 1,
            .comm = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o',
                     '\0'},
        };

        REQUIRE(event.commAsStringView() == "abcdefghijklmno");
        REQUIRE(event.commAsStringView().size() == 15);
    }

    SECTION("empty command name")
    {
        MigrationEvent event{
            .timestamp_ns = 1000,
            .pid = 100,
            .tid = 100,
            .src_cpu = 0,
            .dst_cpu = 1,
            .comm = {'\0'},
        };

        REQUIRE(event.commAsStringView().empty());
    }
}

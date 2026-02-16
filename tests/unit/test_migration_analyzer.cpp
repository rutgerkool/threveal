#include "threveal/analysis/event_store.hpp"
#include "threveal/analysis/migration_analyzer.hpp"
#include "threveal/core/events.hpp"
#include "threveal/core/topology.hpp"
#include "threveal/core/types.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

using Catch::Approx;
using threveal::analysis::EventStore;
using threveal::analysis::MigrationAnalyzer;
using threveal::core::CpuId;
using threveal::core::MigrationEvent;
using threveal::core::MigrationType;
using threveal::core::PmuSample;
using threveal::core::TopologyMap;

namespace
{

auto makeTestTopology() -> TopologyMap
{
    std::vector<CpuId> p_cores = {0, 1, 2, 3, 4, 5};
    std::vector<CpuId> e_cores = {12, 13, 14, 15, 16, 17};
    return TopologyMap{p_cores, e_cores};
}

auto makeMigration(std::uint64_t timestamp_ns, std::uint32_t tid, CpuId src, CpuId dst)
    -> MigrationEvent
{
    MigrationEvent event{
        .timestamp_ns = timestamp_ns,
        .pid = tid,
        .tid = tid,
        .src_cpu = src,
        .dst_cpu = dst,
        .comm = {},
    };
    auto name = "thread_" + std::to_string(tid);
    std::copy_n(name.begin(), std::min(name.size(), event.comm.size() - 1), event.comm.begin());
    return event;
}

auto makePmuSample(std::uint64_t timestamp_ns, std::uint32_t tid, CpuId cpu,
                   std::uint64_t instructions, std::uint64_t cycles, std::uint64_t llc_misses,
                   std::uint64_t llc_references) -> PmuSample
{
    return PmuSample{
        .timestamp_ns = timestamp_ns,
        .tid = tid,
        .cpu_id = cpu,
        .instructions = instructions,
        .cycles = cycles,
        .llc_misses = llc_misses,
        .llc_references = llc_references,
        .branch_misses = 0,
    };
}

auto makeHighPerfSample(std::uint64_t timestamp_ns, std::uint32_t tid, CpuId cpu) -> PmuSample
{
    return makePmuSample(timestamp_ns, tid, cpu, 2'000'000, 1'000'000, 100, 1000);
}

auto makeLowPerfSample(std::uint64_t timestamp_ns, std::uint32_t tid, CpuId cpu) -> PmuSample
{
    return makePmuSample(timestamp_ns, tid, cpu, 1'000'000, 1'000'000, 200, 1000);
}

}  // namespace

TEST_CASE("MigrationAnalyzer with empty store", "[analysis][MigrationAnalyzer]")
{
    EventStore store;
    auto topology = makeTestTopology();
    MigrationAnalyzer analyzer(store, topology);

    auto result = analyzer.analyze();

    REQUIRE(result.total_migrations == 0);
    REQUIRE(result.correlated_migrations == 0);
    REQUIRE(result.impacts.empty());
    REQUIRE(result.type_stats.empty());
    REQUIRE(result.thread_stats.empty());
}

TEST_CASE("MigrationAnalyzer with migrations but no PMU samples", "[analysis][MigrationAnalyzer]")
{
    EventStore store;
    auto topology = makeTestTopology();

    store.addMigration(makeMigration(5'000'000, 42, 0, 12));

    MigrationAnalyzer analyzer(store, topology);
    auto result = analyzer.analyze();

    REQUIRE(result.total_migrations == 1);
    REQUIRE(result.correlated_migrations == 0);
    REQUIRE(result.impacts.size() == 1);
    REQUIRE(result.impacts[0].type == MigrationType::kPToE);
    REQUIRE(result.impacts[0].confidence == 0.0);
    REQUIRE(result.impacts[0].ipc_delta == 0.0);
    REQUIRE(result.impacts[0].cache_miss_delta == 0.0);
}

TEST_CASE("MigrationAnalyzer computes IPC delta correctly", "[analysis][MigrationAnalyzer]")
{
    EventStore store;
    auto topology = makeTestTopology();

    store.addPmuSample(makeHighPerfSample(4'000'000, 42, 0));
    store.addMigration(makeMigration(5'000'000, 42, 0, 12));
    store.addPmuSample(makeLowPerfSample(6'000'000, 42, 12));

    MigrationAnalyzer analyzer(store, topology);
    auto result = analyzer.analyze();

    REQUIRE(result.impacts.size() == 1);
    const auto& impact = result.impacts[0];

    REQUIRE(impact.type == MigrationType::kPToE);
    REQUIRE(impact.ipc_delta == Approx(-1.0));
    REQUIRE(impact.cache_miss_delta == Approx(0.1));
    REQUIRE(impact.confidence > 0.0);
    REQUIRE(impact.confidence <= 1.0);
}

TEST_CASE("MigrationAnalyzer computes positive IPC delta for E-to-P",
          "[analysis][MigrationAnalyzer]")
{
    EventStore store;
    auto topology = makeTestTopology();

    store.addPmuSample(makeLowPerfSample(4'000'000, 42, 12));
    store.addMigration(makeMigration(5'000'000, 42, 12, 0));
    store.addPmuSample(makeHighPerfSample(6'000'000, 42, 0));

    MigrationAnalyzer analyzer(store, topology);
    auto result = analyzer.analyze();

    REQUIRE(result.impacts.size() == 1);
    const auto& impact = result.impacts[0];

    REQUIRE(impact.type == MigrationType::kEToP);
    REQUIRE(impact.ipc_delta == Approx(1.0));
    REQUIRE(impact.cache_miss_delta == Approx(-0.1));
}

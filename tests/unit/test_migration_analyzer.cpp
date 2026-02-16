#include "threveal/analysis/event_store.hpp"
#include "threveal/analysis/migration_analyzer.hpp"
#include "threveal/core/events.hpp"
#include "threveal/core/topology.hpp"
#include "threveal/core/types.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
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

TEST_CASE("MigrationAnalyzer confidence is high for close samples", "[analysis][MigrationAnalyzer]")
{
    EventStore store;
    auto topology = makeTestTopology();

    // 100ns gap on each side
    store.addPmuSample(makeHighPerfSample(4'999'900, 42, 0));
    store.addMigration(makeMigration(5'000'000, 42, 0, 12));
    store.addPmuSample(makeLowPerfSample(5'000'100, 42, 12));

    MigrationAnalyzer analyzer(store, topology);
    auto result = analyzer.analyze();

    REQUIRE(result.impacts.size() == 1);
    REQUIRE(result.impacts[0].confidence > 0.99);
}

TEST_CASE("MigrationAnalyzer confidence decays with distance", "[analysis][MigrationAnalyzer]")
{
    auto topology = makeTestTopology();

    constexpr std::uint64_t kBase = 50'000'000;

    auto compute_confidence = [&](std::uint64_t gap_ns) -> double
    {
        EventStore store;
        store.addPmuSample(makeHighPerfSample(kBase - gap_ns, 42, 0));
        store.addMigration(makeMigration(kBase, 42, 0, 12));
        store.addPmuSample(makeLowPerfSample(kBase + gap_ns, 42, 12));

        MigrationAnalyzer analyzer(store, topology);
        auto result = analyzer.analyze();
        return result.impacts[0].confidence;
    };

    double conf_close = compute_confidence(100'000);
    double conf_medium = compute_confidence(3'000'000);
    double conf_far = compute_confidence(8'000'000);

    REQUIRE(conf_close > conf_medium);
    REQUIRE(conf_medium > conf_far);
    REQUIRE(conf_far > 0.0);
}

TEST_CASE("MigrationAnalyzer confidence is zero beyond max gap", "[analysis][MigrationAnalyzer]")
{
    EventStore store;
    auto topology = makeTestTopology();

    // 15ms gap exceeds default 10ms max
    store.addPmuSample(makeHighPerfSample(0, 42, 0));
    store.addMigration(makeMigration(15'000'000, 42, 0, 12));
    store.addPmuSample(makeLowPerfSample(16'000'000, 42, 12));

    MigrationAnalyzer analyzer(store, topology);
    auto result = analyzer.analyze();

    REQUIRE(result.impacts.size() == 1);
    REQUIRE(result.impacts[0].confidence == 0.0);
    REQUIRE(result.impacts[0].ipc_delta == 0.0);
}

TEST_CASE("MigrationAnalyzer respects custom max sample gap", "[analysis][MigrationAnalyzer]")
{
    EventStore store;
    auto topology = makeTestTopology();

    // 5ms gap on each side
    store.addPmuSample(makeHighPerfSample(0, 42, 0));
    store.addMigration(makeMigration(5'000'000, 42, 0, 12));
    store.addPmuSample(makeLowPerfSample(10'000'000, 42, 12));

    SECTION("within default 10ms gap")
    {
        MigrationAnalyzer analyzer(store, topology);
        auto result = analyzer.analyze();
        REQUIRE(result.impacts[0].confidence > 0.0);
    }

    SECTION("exceeds custom 2ms gap")
    {
        MigrationAnalyzer analyzer(store, topology);
        analyzer.setMaxSampleGap(2'000'000);
        auto result = analyzer.analyze();
        REQUIRE(result.impacts[0].confidence == 0.0);
    }
}

TEST_CASE("MigrationAnalyzer handles only pre-migration sample", "[analysis][MigrationAnalyzer]")
{
    EventStore store;
    auto topology = makeTestTopology();

    store.addPmuSample(makeHighPerfSample(4'000'000, 42, 0));
    store.addMigration(makeMigration(5'000'000, 42, 0, 12));

    MigrationAnalyzer analyzer(store, topology);
    auto result = analyzer.analyze();

    REQUIRE(result.impacts.size() == 1);
    REQUIRE(result.impacts[0].confidence == 0.0);
    REQUIRE(result.impacts[0].ipc_delta == 0.0);
}

TEST_CASE("MigrationAnalyzer handles only post-migration sample", "[analysis][MigrationAnalyzer]")
{
    EventStore store;
    auto topology = makeTestTopology();

    store.addMigration(makeMigration(5'000'000, 42, 0, 12));
    store.addPmuSample(makeLowPerfSample(6'000'000, 42, 12));

    MigrationAnalyzer analyzer(store, topology);
    auto result = analyzer.analyze();

    REQUIRE(result.impacts.size() == 1);
    REQUIRE(result.impacts[0].confidence == 0.0);
    REQUIRE(result.impacts[0].ipc_delta == 0.0);
}

TEST_CASE("MigrationAnalyzer handles samples from wrong thread", "[analysis][MigrationAnalyzer]")
{
    EventStore store;
    auto topology = makeTestTopology();

    // Samples belong to thread 99, migration is for thread 42
    store.addPmuSample(makeHighPerfSample(4'000'000, 99, 0));
    store.addMigration(makeMigration(5'000'000, 42, 0, 12));
    store.addPmuSample(makeLowPerfSample(6'000'000, 99, 12));

    MigrationAnalyzer analyzer(store, topology);
    auto result = analyzer.analyze();

    REQUIRE(result.impacts.size() == 1);
    REQUIRE(result.impacts[0].confidence == 0.0);
}

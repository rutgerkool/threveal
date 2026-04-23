/**
 *  @file       test_perf_event.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Unit tests for the shared perf_event helpers.
 */

#include "threveal/collection/perf_event.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <linux/perf_event.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using threveal::collection::detectCorePmuTypes;
using threveal::collection::kDefaultPmuType;
using threveal::collection::makeEventAttr;
using threveal::collection::PmuEventType;

namespace
{

namespace fs = std::filesystem;

/**
 *  Temporary stand-in for /sys/bus/event_source/devices
 */
class FakeEventSourceDir
{
  public:
    FakeEventSourceDir()
    {
        auto pattern = (fs::temp_directory_path() / "threveal_pmu_XXXXXX").string();
        if (::mkdtemp(pattern.data()) == nullptr)
        {
            throw std::runtime_error("mkdtemp failed");
        }
        root_ = pattern;
    }

    ~FakeEventSourceDir()
    {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    FakeEventSourceDir(const FakeEventSourceDir&) = delete;
    auto operator=(const FakeEventSourceDir&) -> FakeEventSourceDir& = delete;
    FakeEventSourceDir(FakeEventSourceDir&&) = delete;
    auto operator=(FakeEventSourceDir&&) -> FakeEventSourceDir& = delete;

    void addPmu(std::string_view name, std::string_view type) const
    {
        fs::create_directories(root_ / name);
        std::ofstream(root_ / name / "type") << type << '\n';
    }

    [[nodiscard]] auto path() const -> std::string
    {
        return root_.string();
    }

  private:
    fs::path root_;
};

}  // namespace

TEST_CASE("detectCorePmuTypes returns both core PMUs on a hybrid CPU", "[collection][perf_event]")
{
    FakeEventSourceDir dir;
    dir.addPmu("cpu_core", "4");
    dir.addPmu("cpu_atom", "10");

    REQUIRE(detectCorePmuTypes(dir.path()) == std::vector<std::uint32_t>{4, 10});
}

TEST_CASE("detectCorePmuTypes falls back to the default PMU on a non-hybrid CPU",
          "[collection][perf_event]")
{
    FakeEventSourceDir dir;
    dir.addPmu("cpu", "4");

    REQUIRE(detectCorePmuTypes(dir.path()) == std::vector<std::uint32_t>{kDefaultPmuType});
}

TEST_CASE("detectCorePmuTypes returns the remaining PMU when one core type is missing",
          "[collection][perf_event]")
{
    FakeEventSourceDir dir;
    dir.addPmu("cpu_core", "4");

    REQUIRE(detectCorePmuTypes(dir.path()) == std::vector<std::uint32_t>{4});
}

TEST_CASE("detectCorePmuTypes ignores an unparsable type file", "[collection][perf_event]")
{
    FakeEventSourceDir dir;
    dir.addPmu("cpu_core", "not-a-number");
    dir.addPmu("cpu_atom", "10");

    REQUIRE(detectCorePmuTypes(dir.path()) == std::vector<std::uint32_t>{10});
}

TEST_CASE("detectCorePmuTypes falls back to the default PMU when the directory is missing",
          "[collection][perf_event]")
{
    REQUIRE(detectCorePmuTypes("/nonexistent/threveal") ==
            std::vector<std::uint32_t>{kDefaultPmuType});
}

TEST_CASE("detectCorePmuTypes never returns an empty list on this machine",
          "[collection][perf_event]")
{
    REQUIRE_FALSE(detectCorePmuTypes().empty());
}

TEST_CASE("makeEventAttr leaves the config untouched for the default PMU",
          "[collection][perf_event]")
{
    auto attr = makeEventAttr(PmuEventType::kCycles);

    REQUIRE(attr.type == PERF_TYPE_HARDWARE);
    REQUIRE(attr.config == PERF_COUNT_HW_CPU_CYCLES);
}

TEST_CASE("makeEventAttr encodes the PMU type in the upper config bits", "[collection][perf_event]")
{
    constexpr std::uint32_t kAtomPmuType = 10;

    auto event =
        GENERATE(PmuEventType::kCycles, PmuEventType::kInstructions, PmuEventType::kLlcLoads,
                 PmuEventType::kLlcLoadMisses, PmuEventType::kBranchMisses);

    auto generic = makeEventAttr(event);
    auto targeted = makeEventAttr(event, kAtomPmuType);

    REQUIRE(targeted.type == generic.type);
    REQUIRE((targeted.config & PERF_HW_EVENT_MASK) == generic.config);
    REQUIRE((targeted.config >> PERF_PMU_TYPE_SHIFT) == kAtomPmuType);
}

/**
 *  @file       test_perf_event.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Unit tests for the shared perf_event helpers.
 */

#include "threveal/collection/perf_event.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using threveal::collection::detectCorePmuTypes;
using threveal::collection::kDefaultPmuType;

namespace
{

namespace fs = std::filesystem;

/**
 *  Temporary stand-in for /sys/bus/event_source/devices, removed on destruction.
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

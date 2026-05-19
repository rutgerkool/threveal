/**
 *  @file       test_thread_cpu.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Unit tests for the thread CPU lookup.
 */

#include "threveal/collection/thread_cpu.hpp"

#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unistd.h>

#include "pmu_test_support.hpp"

using threveal::collection::parseLastCpu;
using threveal::collection::readLastCpu;
using threveal::test::ScopedCpuPin;

namespace
{

/**
 *  Builds a /proc stat line with the given comm and processor field.
 */
auto makeStatLine(std::string_view comm, std::string_view processor) -> std::string
{
    std::string line = "1234 (" + std::string(comm) + ") R";
    for (int field = 4; field <= 52; ++field)
    {
        line += ' ';
        line += (field == 39) ? std::string(processor) : std::string("0");
    }
    return line;
}

}  // namespace

TEST_CASE("parseLastCpu reads the processor field", "[collection][thread_cpu]")
{
    REQUIRE(parseLastCpu(makeStatLine("worker", "7")) == 7U);
}

TEST_CASE("parseLastCpu handles a comm with spaces and parentheses", "[collection][thread_cpu]")
{
    REQUIRE(parseLastCpu(makeStatLine("my ) odd (name", "13")) == 13U);
}

TEST_CASE("parseLastCpu rejects malformed stat lines", "[collection][thread_cpu]")
{
    SECTION("empty line")
    {
        REQUIRE_FALSE(parseLastCpu("").has_value());
    }

    SECTION("no comm field")
    {
        REQUIRE_FALSE(parseLastCpu("1234 worker R 0 0").has_value());
    }

    SECTION("too few fields")
    {
        REQUIRE_FALSE(parseLastCpu("1234 (worker) R 0 0 0").has_value());
    }

    SECTION("non-numeric processor field")
    {
        REQUIRE_FALSE(parseLastCpu(makeStatLine("worker", "x")).has_value());
    }

    SECTION("processor field with trailing characters")
    {
        REQUIRE_FALSE(parseLastCpu(makeStatLine("worker", "7x")).has_value());
    }
}

TEST_CASE("readLastCpu reports the CPU the calling thread is pinned to", "[collection][thread_cpu]")
{
    ScopedCpuPin pin(0);
    REQUIRE(pin.pinned());

    REQUIRE(readLastCpu(gettid()) == 0U);
}

TEST_CASE("readLastCpu returns nullopt for a thread that does not exist",
          "[collection][thread_cpu]")
{
    REQUIRE_FALSE(readLastCpu(999'999'999).has_value());
}

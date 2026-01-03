/**
 *  @file       test_topology.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Unit tests for CPU topology detection.
 */

#include "threveal/core/errors.hpp"
#include "threveal/core/topology.hpp"
#include "threveal/core/types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace threveal::core;

TEST_CASE("parseCpuList parses single numbers", "[topology][parseCpuList]")
{
    SECTION("single digit")
    {
        auto result = parseCpuList("0");
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 1);
        REQUIRE((*result)[0] == 0);
    }

    SECTION("multi-digit number")
    {
        auto result = parseCpuList("42");
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 1);
        REQUIRE((*result)[0] == 42);
    }

    SECTION("large number")
    {
        auto result = parseCpuList("255");
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 1);
        REQUIRE((*result)[0] == 255);
    }
}

TEST_CASE("parseCpuList parses ranges", "[topology][parseCpuList]")
{
    SECTION("simple range")
    {
        auto result = parseCpuList("0-3");
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 4);
        REQUIRE((*result)[0] == 0);
        REQUIRE((*result)[1] == 1);
        REQUIRE((*result)[2] == 2);
        REQUIRE((*result)[3] == 3);
    }

    SECTION("range starting from non-zero")
    {
        auto result = parseCpuList("12-15");
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 4);
        REQUIRE((*result)[0] == 12);
        REQUIRE((*result)[3] == 15);
    }

    SECTION("single element range")
    {
        auto result = parseCpuList("5-5");
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 1);
        REQUIRE((*result)[0] == 5);
    }
}

TEST_CASE("parseCpuList parses comma-separated values", "[topology][parseCpuList]")
{
    SECTION("two numbers")
    {
        auto result = parseCpuList("0,2");
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 2);
        REQUIRE((*result)[0] == 0);
        REQUIRE((*result)[1] == 2);
    }

    SECTION("multiple numbers")
    {
        auto result = parseCpuList("0,2,4,6");
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 4);
        REQUIRE((*result)[0] == 0);
        REQUIRE((*result)[1] == 2);
        REQUIRE((*result)[2] == 4);
        REQUIRE((*result)[3] == 6);
    }
}

TEST_CASE("parseCpuList parses mixed format", "[topology][parseCpuList]")
{
    SECTION("range followed by number")
    {
        auto result = parseCpuList("0-3,8");
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 5);
        REQUIRE((*result)[0] == 0);
        REQUIRE((*result)[3] == 3);
        REQUIRE((*result)[4] == 8);
    }

    SECTION("number followed by range")
    {
        auto result = parseCpuList("0,4-7");
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 5);
        REQUIRE((*result)[0] == 0);
        REQUIRE((*result)[1] == 4);
        REQUIRE((*result)[4] == 7);
    }

    SECTION("typical hybrid CPU format - i7-13700H style")
    {
        // P-cores: 0-11 (6 cores, 12 threads with SMT)
        // E-cores: 12-19 (8 cores, 8 threads)
        auto p_cores = parseCpuList("0-11");
        REQUIRE(p_cores.has_value());
        REQUIRE(p_cores->size() == 12);

        auto e_cores = parseCpuList("12-19");
        REQUIRE(e_cores.has_value());
        REQUIRE(e_cores->size() == 8);
    }

    SECTION("complex mixed format")
    {
        auto result = parseCpuList("0-2,5,8-10,15");
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 8);
        // 0, 1, 2, 5, 8, 9, 10, 15
        REQUIRE((*result)[0] == 0);
        REQUIRE((*result)[2] == 2);
        REQUIRE((*result)[3] == 5);
        REQUIRE((*result)[4] == 8);
        REQUIRE((*result)[7] == 15);
    }
}

TEST_CASE("parseCpuList handles whitespace", "[topology][parseCpuList]")
{
    SECTION("leading whitespace")
    {
        auto result = parseCpuList("  0-3");
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 4);
    }

    SECTION("trailing whitespace")
    {
        auto result = parseCpuList("0-3  ");
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 4);
    }

    SECTION("trailing newline (typical for sysfs)")
    {
        auto result = parseCpuList("0-3\n");
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 4);
    }

    SECTION("whitespace around comma")
    {
        auto result = parseCpuList("0 , 2");
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 2);
    }
}

TEST_CASE("parseCpuList rejects invalid input", "[topology][parseCpuList]")
{
    SECTION("empty string")
    {
        auto result = parseCpuList("");
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == TopologyError::kParseError);
    }

    SECTION("whitespace only")
    {
        auto result = parseCpuList("   ");
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == TopologyError::kParseError);
    }

    SECTION("inverted range")
    {
        auto result = parseCpuList("5-3");
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == TopologyError::kParseError);
    }

    SECTION("non-numeric input")
    {
        auto result = parseCpuList("abc");
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == TopologyError::kParseError);
    }

    SECTION("negative number")
    {
        auto result = parseCpuList("-1");
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == TopologyError::kParseError);
    }

    SECTION("trailing comma")
    {
        auto result = parseCpuList("0,1,");
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == TopologyError::kParseError);
    }
}

TEST_CASE("TopologyMap construction and basic queries", "[topology][TopologyMap]")
{
    std::vector<CpuId> p_cores = {0, 1, 2, 3, 4, 5};
    std::vector<CpuId> e_cores = {6, 7, 8, 9};

    TopologyMap map(p_cores, e_cores);

    SECTION("totalCpuCount returns sum of both core types")
    {
        REQUIRE(map.totalCpuCount() == 10);
    }

    SECTION("isHybrid returns true when both core types present")
    {
        REQUIRE(map.isHybrid() == true);
    }

    SECTION("getPCores returns correct span")
    {
        auto result = map.getPCores();
        REQUIRE(result.size() == 6);
        REQUIRE(result[0] == 0);
        REQUIRE(result[5] == 5);
    }

    SECTION("getECores returns correct span")
    {
        auto result = map.getECores();
        REQUIRE(result.size() == 4);
        REQUIRE(result[0] == 6);
        REQUIRE(result[3] == 9);
    }
}

TEST_CASE("TopologyMap getCoreType classification", "[topology][TopologyMap]")
{
    std::vector<CpuId> p_cores = {0, 1, 2, 3};
    std::vector<CpuId> e_cores = {8, 9, 10, 11};

    TopologyMap map(p_cores, e_cores);

    SECTION("P-cores are correctly identified")
    {
        for (CpuId cpu : p_cores)
        {
            auto result = map.getCoreType(cpu);
            REQUIRE(result.has_value());
            REQUIRE(*result == CoreType::kPCore);
        }
    }

    SECTION("E-cores are correctly identified")
    {
        for (CpuId cpu : e_cores)
        {
            auto result = map.getCoreType(cpu);
            REQUIRE(result.has_value());
            REQUIRE(*result == CoreType::kECore);
        }
    }

    SECTION("gap CPUs return error")
    {
        // CPU 5 is between P-cores and E-cores, not in either list
        auto result = map.getCoreType(5);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == TopologyError::kInvalidCpuId);
    }

    SECTION("out of range CPU returns error")
    {
        auto result = map.getCoreType(99);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == TopologyError::kInvalidCpuId);
    }
}

TEST_CASE("TopologyMap handles non-hybrid configurations", "[topology][TopologyMap]")
{
    SECTION("P-cores only")
    {
        std::vector<CpuId> p_cores = {0, 1, 2, 3};
        std::vector<CpuId> e_cores = {};

        TopologyMap map(p_cores, e_cores);

        REQUIRE(map.isHybrid() == false);
        REQUIRE(map.totalCpuCount() == 4);
        REQUIRE(map.getPCores().size() == 4);
        REQUIRE(map.getECores().empty());
    }

    SECTION("E-cores only")
    {
        std::vector<CpuId> p_cores = {};
        std::vector<CpuId> e_cores = {0, 1, 2, 3};

        TopologyMap map(p_cores, e_cores);

        REQUIRE(map.isHybrid() == false);
        REQUIRE(map.totalCpuCount() == 4);
        REQUIRE(map.getPCores().empty());
        REQUIRE(map.getECores().size() == 4);
    }

    SECTION("empty topology")
    {
        std::vector<CpuId> p_cores = {};
        std::vector<CpuId> e_cores = {};

        TopologyMap map(p_cores, e_cores);

        REQUIRE(map.isHybrid() == false);
        REQUIRE(map.totalCpuCount() == 0);
    }
}

TEST_CASE("TopologyMap handles realistic i7-13700H topology", "[topology][TopologyMap]")
{
    // i7-13700H: 6 P-cores (12 threads) + 8 E-cores (8 threads) = 20 threads
    std::vector<CpuId> p_cores = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    std::vector<CpuId> e_cores = {12, 13, 14, 15, 16, 17, 18, 19};

    TopologyMap map(p_cores, e_cores);

    REQUIRE(map.isHybrid() == true);
    REQUIRE(map.totalCpuCount() == 20);

    // Verify all P-cores
    for (CpuId cpu = 0; cpu <= 11; ++cpu)
    {
        auto result = map.getCoreType(cpu);
        REQUIRE(result.has_value());
        REQUIRE(*result == CoreType::kPCore);
    }

    // Verify all E-cores
    for (CpuId cpu = 12; cpu <= 19; ++cpu)
    {
        auto result = map.getCoreType(cpu);
        REQUIRE(result.has_value());
        REQUIRE(*result == CoreType::kECore);
    }
}

TEST_CASE("parseCoreType parses P-core identifiers", "[topology][parseCoreType]")
{
    SECTION("modern kernel format")
    {
        auto result = parseCoreType("Core");
        REQUIRE(result.has_value());
        REQUIRE(*result == CoreType::kPCore);
    }

    SECTION("older kernel format")
    {
        auto result = parseCoreType("intel_core");
        REQUIRE(result.has_value());
        REQUIRE(*result == CoreType::kPCore);
    }

    SECTION("with trailing newline")
    {
        auto result = parseCoreType("Core\n");
        REQUIRE(result.has_value());
        REQUIRE(*result == CoreType::kPCore);
    }

    SECTION("with surrounding whitespace")
    {
        auto result = parseCoreType("  Core  ");
        REQUIRE(result.has_value());
        REQUIRE(*result == CoreType::kPCore);
    }
}

TEST_CASE("parseCoreType parses E-core identifiers", "[topology][parseCoreType]")
{
    SECTION("modern kernel format")
    {
        auto result = parseCoreType("Atom");
        REQUIRE(result.has_value());
        REQUIRE(*result == CoreType::kECore);
    }

    SECTION("older kernel format")
    {
        auto result = parseCoreType("intel_atom");
        REQUIRE(result.has_value());
        REQUIRE(*result == CoreType::kECore);
    }

    SECTION("with trailing newline")
    {
        auto result = parseCoreType("Atom\n");
        REQUIRE(result.has_value());
        REQUIRE(*result == CoreType::kECore);
    }

    SECTION("with surrounding whitespace")
    {
        auto result = parseCoreType("  Atom  ");
        REQUIRE(result.has_value());
        REQUIRE(*result == CoreType::kECore);
    }
}

TEST_CASE("parseCoreType rejects invalid input", "[topology][parseCoreType]")
{
    SECTION("empty string")
    {
        auto result = parseCoreType("");
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == TopologyError::kParseError);
    }

    SECTION("whitespace only")
    {
        auto result = parseCoreType("   ");
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == TopologyError::kParseError);
    }

    SECTION("unknown core type")
    {
        auto result = parseCoreType("Unknown");
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == TopologyError::kParseError);
    }

    SECTION("case sensitive - lowercase")
    {
        auto result = parseCoreType("core");
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == TopologyError::kParseError);
    }

    SECTION("case sensitive - uppercase")
    {
        auto result = parseCoreType("ATOM");
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == TopologyError::kParseError);
    }

    SECTION("numeric input")
    {
        auto result = parseCoreType("0");
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == TopologyError::kParseError);
    }
}

TEST_CASE("TopologyMap isSmtSibling without SMT data", "[topology][TopologyMap][smt]")
{
    // When constructed directly (not via loadFromSysfs), SMT data is unavailable
    std::vector<CpuId> p_cores = {0, 1, 2, 3};
    std::vector<CpuId> e_cores = {4, 5, 6, 7};

    TopologyMap map(p_cores, e_cores);

    SECTION("returns false when SMT data unavailable")
    {
        REQUIRE(map.isSmtSibling(0, 1) == false);
        REQUIRE(map.isSmtSibling(0, 2) == false);
        REQUIRE(map.isSmtSibling(4, 5) == false);
    }

    SECTION("returns false for same CPU")
    {
        REQUIRE(map.isSmtSibling(0, 0) == false);
        REQUIRE(map.isSmtSibling(4, 4) == false);
    }

    SECTION("returns false for out of range CPUs")
    {
        REQUIRE(map.isSmtSibling(0, 99) == false);
        REQUIRE(map.isSmtSibling(99, 0) == false);
        REQUIRE(map.isSmtSibling(99, 100) == false);
    }
}

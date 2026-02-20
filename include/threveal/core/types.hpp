/**
 *  @file       types.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Core type definitions for the Threveal profiler.
 */

#ifndef THREVEAL_CORE_TYPES_HPP_
#define THREVEAL_CORE_TYPES_HPP_

#include <cstdint>
#include <limits>
#include <string_view>

namespace threveal::core
{

/**
 *  Type alias for logical CPU identifiers.
 */
using CpuId = std::uint32_t;

/**
 *  Sentinel value indicating an invalid or uninitialized CPU ID.
 */
inline constexpr CpuId kInvalidCpuId = std::numeric_limits<CpuId>::max();

/**
 *  Classification of CPU core types on Intel hybrid architectures.
 */
enum class CoreType : std::uint8_t
{
    /**
     *  Core type could not be determined.
     */
    kUnknown = 0,

    /**
     *  Performance core (Golden Cove / Raptor Cove).
     */
    kPCore = 1,

    /**
     *  Efficiency core (Gracemont).
     */
    kECore = 2,
};

/**
 *  Converts a CoreType to its human-readable string representation.
 *
 *  @param      type  The core type to convert.
 *  @return     A string view containing "P-core", "E-core", "Unknown", or "Invalid".
 */
[[nodiscard]] constexpr auto toString(CoreType type) noexcept -> std::string_view
{
    switch (type)
    {
        case CoreType::kPCore:
            return "P-core";
        case CoreType::kECore:
            return "E-core";
        case CoreType::kUnknown:
            return "Unknown";
    }
    return "Invalid";
}

}  // namespace threveal::core

#endif  // THREVEAL_CORE_TYPES_HPP_

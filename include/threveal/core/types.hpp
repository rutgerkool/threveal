/**
 *  @file       types.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Core type definitions for the Threveal profiler.
 *
 *  Defines fundamental types used throughout Threveal for representing
 *  CPU identifiers and core type classifications on Intel hybrid architectures.
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
 *
 *  Represents the logical CPU number as seen by the Linux kernel (0-based).
 *  On a hybrid system like i7-13700H, valid values are 0-19.
 */
using CpuId = std::uint32_t;

/**
 *  Sentinel value indicating an invalid or uninitialized CPU ID.
 */
inline constexpr CpuId kInvalidCpuId = std::numeric_limits<CpuId>::max();

/**
 *  Classification of CPU core types on Intel hybrid architectures.
 *
 *  Intel Alder Lake and later processors feature heterogeneous cores:
 *  - P-cores (Performance): High IPC, wide execution, SMT capable
 *  - E-cores (Efficiency): Lower power, narrower execution, no SMT
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

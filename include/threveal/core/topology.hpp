/**
 *  @file       topology.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  CPU topology detection for Intel hybrid architectures.
 */

#ifndef THREVEAL_CORE_TOPOLOGY_HPP_
#define THREVEAL_CORE_TOPOLOGY_HPP_

#include "threveal/core/errors.hpp"
#include "threveal/core/types.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

namespace threveal::core
{

/**
 *  Maps logical CPU IDs to their core type classification.
 */
class TopologyMap
{
  public:
    /**
     *  Constructs an empty TopologyMap.
     */
    TopologyMap() = default;

    /**
     *  Constructs a TopologyMap from known P-core and E-core CPU lists.
     *
     *  @param      p_cores  Span of CPU IDs that are Performance cores.
     *  @param      e_cores  Span of CPU IDs that are Efficiency cores.
     */
    TopologyMap(std::span<const CpuId> p_cores, std::span<const CpuId> e_cores);

    /**
     *  Retrieves the core type for a given CPU ID.
     *
     *  @param      cpu_id  The logical CPU identifier (0-based).
     *  @return     The core type on success, or TopologyError::kInvalidCpuId
     *              if the CPU ID is out of range.
     */
    [[nodiscard]] auto getCoreType(CpuId cpu_id) const -> std::expected<CoreType, TopologyError>;

    /**
     *  Returns a view of all P-core CPU IDs.
     *
     *  @return     A span containing all Performance core CPU IDs.
     */
    [[nodiscard]] auto getPCores() const noexcept -> std::span<const CpuId>;

    /**
     *  Returns a view of all E-core CPU IDs.
     *
     *  @return     A span containing all Efficiency core CPU IDs.
     */
    [[nodiscard]] auto getECores() const noexcept -> std::span<const CpuId>;

    /**
     *  Returns the total number of CPUs in the topology.
     *
     *  @return     The count of all CPUs (P-cores + E-cores).
     */
    [[nodiscard]] auto totalCpuCount() const noexcept -> std::size_t;

    /**
     *  Checks if the topology represents a hybrid CPU.
     *
     *  @return     True if both P-cores and E-cores are present.
     */
    [[nodiscard]] auto isHybrid() const noexcept -> bool;

    /**
     *  Checks if two CPUs are SMT (hyperthreading) siblings.
     *
     *  @param      cpu_a  First logical CPU identifier.
     *  @param      cpu_b  Second logical CPU identifier.
     *  @return     True if both CPUs share the same physical core.
     *              Returns false if SMT data is unavailable or CPUs are invalid.
     */
    [[nodiscard]] auto isSmtSibling(CpuId cpu_a, CpuId cpu_b) const noexcept -> bool;

    /**
     *  Loads CPU topology from sysfs.
     *
     *  @return     A populated TopologyMap on success, or a TopologyError
     *              indicating why detection failed.
     */
    [[nodiscard]] static auto loadFromSysfs() -> std::expected<TopologyMap, TopologyError>;

  private:
    /**
     *  Builds the CPU ID to CoreType lookup table.
     */
    void buildLookupTable();

    /**
     *  Loads SMT sibling data from sysfs.
     */
    void loadSmtData();

    std::vector<CpuId> p_cores_;
    std::vector<CpuId> e_cores_;
    std::vector<CoreType> cpu_to_type_;
    std::vector<CpuId> physical_core_id_;
};

/**
 *  Parses a CPU list string in sysfs format.
 *
 *  @param      content  The CPU list string to parse (e.g., "0-5,12-19").
 *  @return     A vector of CPU IDs on success, or TopologyError::kParseError
 *              if the format is invalid.
 */
[[nodiscard]] auto parseCpuList(std::string_view content)
    -> std::expected<std::vector<CpuId>, TopologyError>;

/**
 *  Parses a core_type sysfs string to determine the core type.
 *
 *  @param      content  The core_type string to parse (e.g., "Core", "Atom").
 *  @return     The CoreType on success, or TopologyError::kParseError
 *              if the format is not recognized.
 */
[[nodiscard]] auto parseCoreType(std::string_view content)
    -> std::expected<CoreType, TopologyError>;

}  // namespace threveal::core

#endif  // THREVEAL_CORE_TOPOLOGY_HPP_

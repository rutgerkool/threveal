/**
 *  @file       topology.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  CPU topology detection for Intel hybrid architectures.
 *
 *  Provides functionality to detect P-cores and E-cores on Intel Alder Lake
 *  and Raptor Lake processors by parsing sysfs entries.
 */

#ifndef THREVEAL_CORE_TOPOLOGY_HPP_
#define THREVEAL_CORE_TOPOLOGY_HPP_

#include "threveal/core/errors.hpp"
#include "threveal/core/types.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace threveal::core
{

/**
 *  Maps logical CPU IDs to their core type classification.
 *
 *  TopologyMap provides efficient lookup of whether a given CPU ID corresponds
 *  to a Performance core (P-core) or Efficiency core (E-core) on Intel hybrid
 *  architectures. It is typically constructed by parsing sysfs entries at
 *  program startup.
 */
class TopologyMap
{
  public:
    /**
     *  Constructs an empty TopologyMap.
     *
     *  An empty map will return kUnknown for all CPU IDs. Use loadFromSysfs()
     *  to construct a properly initialized map.
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
     *  Queries the internal topology map to determine whether the specified
     *  CPU is a Performance core (P-core) or Efficiency core (E-core).
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
     *  A hybrid CPU has both P-cores and E-cores.
     *
     *  @return     True if both P-cores and E-cores are present.
     */
    [[nodiscard]] auto isHybrid() const noexcept -> bool;

    /**
     *  Loads CPU topology from sysfs.
     *
     *  Parses /sys/devices/cpu_core/cpus and /sys/devices/cpu_atom/cpus
     *  to determine which CPUs are P-cores and E-cores.
     *
     *  @return     A populated TopologyMap on success, or a TopologyError
     *              indicating why detection failed.
     */
    [[nodiscard]] static auto loadFromSysfs() -> std::expected<TopologyMap, TopologyError>;

  private:
    /**
     *  Builds the CPU ID to CoreType lookup table.
     *
     *  Called after p_cores_ and e_cores_ are populated to create
     *  an O(1) lookup structure.
     */
    void buildLookupTable();

    std::vector<CpuId> p_cores_;
    std::vector<CpuId> e_cores_;
    std::vector<CoreType> cpu_to_type_;
};

}  // namespace threveal::core

#endif  // THREVEAL_CORE_TOPOLOGY_HPP_

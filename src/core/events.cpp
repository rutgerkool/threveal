/**
 *  @file       events.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Implementation of event classification functions.
 */

#include "threveal/core/events.hpp"

#include "threveal/core/topology.hpp"
#include "threveal/core/types.hpp"

namespace threveal::core
{

auto classifyMigration(const MigrationEvent& event, const TopologyMap& topology) -> MigrationType
{
    auto src_type = topology.getCoreType(event.src_cpu);
    auto dst_type = topology.getCoreType(event.dst_cpu);

    // If either CPU lookup fails, we can't classify
    if (!src_type || !dst_type)
    {
        return MigrationType::kUnknown;
    }

    // Classify based on source and destination core types
    if (*src_type == CoreType::kPCore)
    {
        if (*dst_type == CoreType::kPCore)
        {
            return MigrationType::kPToP;
        }
        return MigrationType::kPToE;
    }

    // Source is E-core
    if (*dst_type == CoreType::kPCore)
    {
        return MigrationType::kEToP;
    }
    return MigrationType::kEToE;
}

}  // namespace threveal::core

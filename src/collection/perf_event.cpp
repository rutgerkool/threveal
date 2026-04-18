/**
 *  @file       perf_event.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Implementation of the shared perf_event_open() helpers.
 */

#include "threveal/collection/perf_event.hpp"

#include "threveal/core/errors.hpp"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <linux/perf_event.h>
#include <optional>
#include <string_view>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace threveal::collection
{

namespace
{

/**
 *  Core PMUs exposed by the kernel on hybrid Intel CPUs.
 */
constexpr std::array<std::string_view, 2> kHybridCorePmus = {"cpu_core", "cpu_atom"};

/**
 *  Configures a perf_event_attr structure for a hardware event.
 *
 *  @param      config  The PERF_COUNT_HW_* constant for the desired event.
 *  @return     Configured perf_event_attr structure ready for perf_event_open().
 */
auto makeHardwareEventAttr(std::uint64_t config) -> perf_event_attr
{
    perf_event_attr attr{};

    // Zero-initialize to ensure all fields have defined values
    std::memset(&attr, 0, sizeof(attr));

    attr.type = PERF_TYPE_HARDWARE;

    // Required for kernel version compatibility
    attr.size = sizeof(attr);

    // The specific hardware event (cycles, instructions, etc.)
    attr.config = config;

    // Start disabled so caller can set up multiple counters before enabling
    attr.disabled = 1;

    // Exclude kernel and hypervisor to avoid needing elevated privileges
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;

    return attr;
}

/**
 *  Configures a perf_event_attr structure for a cache event.
 *
 *  @param      cache_id   The cache level (e.g., PERF_COUNT_HW_CACHE_LL).
 *  @param      op_id      The operation (e.g., PERF_COUNT_HW_CACHE_OP_READ).
 *  @param      result_id  The result type (e.g., PERF_COUNT_HW_CACHE_RESULT_MISS).
 *  @return     Configured perf_event_attr structure ready for perf_event_open().
 */
auto makeCacheEventAttr(std::uint64_t cache_id, std::uint64_t op_id, std::uint64_t result_id)
    -> perf_event_attr
{
    perf_event_attr attr{};
    std::memset(&attr, 0, sizeof(attr));

    attr.type = PERF_TYPE_HW_CACHE;
    attr.size = sizeof(attr);

    // Encode cache_id, operation, and result into the config field.
    attr.config = cache_id | (op_id << 8) | (result_id << 16);

    attr.disabled = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;

    return attr;
}

/**
 *  Reads the numeric type of a PMU from its sysfs directory.
 *
 *  @param      pmu_dir  The PMU's directory under the event source directory.
 *  @return     The PMU type, or std::nullopt if the PMU does not exist.
 */
auto readPmuType(const std::filesystem::path& pmu_dir) -> std::optional<std::uint32_t>
{
    std::ifstream file(pmu_dir / "type");
    std::uint32_t type = 0;
    if (!(file >> type))
    {
        return std::nullopt;
    }
    return type;
}

}  // namespace

auto perfEventOpen(perf_event_attr* attr, pid_t pid, int cpu, int group_fd, unsigned long flags)
    -> int
{
    return static_cast<int>(syscall(SYS_perf_event_open, attr, pid, cpu, group_fd, flags));
}

auto makeEventAttr(PmuEventType event) -> perf_event_attr
{
    switch (event)
    {
        case PmuEventType::kCycles:

            // Total CPU cycles elapsed
            return makeHardwareEventAttr(PERF_COUNT_HW_CPU_CYCLES);

        case PmuEventType::kInstructions:

            // Retired instructions
            return makeHardwareEventAttr(PERF_COUNT_HW_INSTRUCTIONS);

        case PmuEventType::kBranchMisses:

            // Branch predictions that were incorrect
            return makeHardwareEventAttr(PERF_COUNT_HW_BRANCH_MISSES);

        case PmuEventType::kLlcLoads:

            // Last-level cache read accesses (hits + misses)
            return makeCacheEventAttr(PERF_COUNT_HW_CACHE_LL, PERF_COUNT_HW_CACHE_OP_READ,
                                      PERF_COUNT_HW_CACHE_RESULT_ACCESS);

        case PmuEventType::kLlcLoadMisses:

            // Last-level cache read misses
            return makeCacheEventAttr(PERF_COUNT_HW_CACHE_LL, PERF_COUNT_HW_CACHE_OP_READ,
                                      PERF_COUNT_HW_CACHE_RESULT_MISS);
    }

    // Unreachable if all enum cases handled, but provides safe fallback
    return makeHardwareEventAttr(PERF_COUNT_HW_CPU_CYCLES);
}

auto errnoToPmuError(int err) noexcept -> core::PmuError
{
    switch (err)
    {
        case EACCES:
        case EPERM:

            // User lacks CAP_PERFMON capability or perf_event_paranoid is too high.
            return core::PmuError::kPermissionDenied;

        case ENOENT:
        case ENODEV:
        case EOPNOTSUPP:

            // The requested event is not available on this CPU or kernel.
            // This can happen with cache events on some microarchitectures.
            return core::PmuError::kEventNotSupported;

        case ESRCH:
        case EINVAL:

            // Invalid PID/TID specified, or invalid combination of parameters
            return core::PmuError::kInvalidTarget;

        case EMFILE:
        case ENFILE:

            // Too many open file descriptors or PMU hardware counters exhausted.
            return core::PmuError::kTooManyEvents;

        default:
            return core::PmuError::kOpenFailed;
    }
}

auto detectCorePmuTypes(std::string_view event_source_dir) -> std::vector<std::uint32_t>
{
    std::vector<std::uint32_t> types;
    for (auto pmu : kHybridCorePmus)
    {
        if (auto type = readPmuType(std::filesystem::path(event_source_dir) / pmu))
        {
            types.push_back(*type);
        }
    }

    // Non-hybrid CPUs have a single core PMU that generic events already target
    if (types.empty())
    {
        types.push_back(kDefaultPmuType);
    }
    return types;
}

}  // namespace threveal::collection

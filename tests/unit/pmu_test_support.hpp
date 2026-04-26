/**
 *  @file       pmu_test_support.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Helpers shared by the PMU test suites.
 */

#ifndef THREVEAL_TESTS_UNIT_PMU_TEST_SUPPORT_HPP_
#define THREVEAL_TESTS_UNIT_PMU_TEST_SUPPORT_HPP_

#include "threveal/core/types.hpp"

#include <cstdint>
#include <fstream>
#include <sched.h>

namespace threveal::test
{

/**
 *  Checks if PMU access is permitted on this system.
 *
 *  @return     True if perf_event_paranoid allows user-space PMU access.
 */
inline auto hasPmuAccess() -> bool
{
    std::ifstream file("/proc/sys/kernel/perf_event_paranoid");
    if (!file)
    {
        return false;
    }

    int level = 0;
    file >> level;

    // Level <= 1 allows user-space PMU access without CAP_PERFMON
    return level <= 1;
}

/**
 *  Runs a short loop that the compiler cannot optimise away.
 */
inline void burnCycles()
{
    volatile std::uint64_t sum = 0;
    for (std::uint64_t i = 0; i < 100000; ++i)
    {
        sum += i;
    }
}

/**
 *  Pins the calling thread to one CPU and restores its previous affinity on destruction.
 */
class ScopedCpuPin
{
  public:
    explicit ScopedCpuPin(core::CpuId cpu) noexcept
    {
        cpu_set_t target{};
        CPU_ZERO(&target);
        CPU_SET(cpu, &target);

        pinned_ = sched_getaffinity(0, sizeof(previous_), &previous_) == 0 &&
                  sched_setaffinity(0, sizeof(target), &target) == 0;
    }

    ~ScopedCpuPin()
    {
        if (pinned_)
        {
            sched_setaffinity(0, sizeof(previous_), &previous_);
        }
    }

    ScopedCpuPin(const ScopedCpuPin&) = delete;
    auto operator=(const ScopedCpuPin&) -> ScopedCpuPin& = delete;
    ScopedCpuPin(ScopedCpuPin&&) = delete;
    auto operator=(ScopedCpuPin&&) -> ScopedCpuPin& = delete;

    [[nodiscard]] auto pinned() const noexcept -> bool
    {
        return pinned_;
    }

  private:
    cpu_set_t previous_{};
    bool pinned_ = false;
};

}  // namespace threveal::test

#endif  // THREVEAL_TESTS_UNIT_PMU_TEST_SUPPORT_HPP_

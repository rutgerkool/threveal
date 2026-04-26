/**
 *  @file       pmu_test_support.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Helpers shared by the PMU test suites.
 */

#ifndef THREVEAL_TESTS_UNIT_PMU_TEST_SUPPORT_HPP_
#define THREVEAL_TESTS_UNIT_PMU_TEST_SUPPORT_HPP_

#include <fstream>

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

}  // namespace threveal::test

#endif  // THREVEAL_TESTS_UNIT_PMU_TEST_SUPPORT_HPP_

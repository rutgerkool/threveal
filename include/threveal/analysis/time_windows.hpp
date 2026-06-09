/**
 *  @file       time_windows.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Time-windowed view of a thread's migrations and performance.
 */

#ifndef THREVEAL_ANALYSIS_TIME_WINDOWS_HPP_
#define THREVEAL_ANALYSIS_TIME_WINDOWS_HPP_

#include "threveal/analysis/pmu_interpolation.hpp"
#include "threveal/core/events.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace threveal::analysis
{

/**
 *  A migration reduced to what the time windows need.
 */
struct TimedMigration
{
    std::uint64_t timestamp_ns = 0;
    core::MigrationType type = core::MigrationType::kUnknown;
};

/**
 *  Activity of one thread during one time window.
 */
struct ThreadWindow
{
    std::uint64_t start_ns = 0;
    std::uint64_t end_ns = 0;
    std::uint32_t p_to_e_migrations = 0;
    std::uint32_t e_to_p_migrations = 0;
    std::uint32_t p_to_p_migrations = 0;
    std::uint32_t e_to_e_migrations = 0;

    /**
     *  Rates over the window, or std::nullopt if the thread was not sampled in it.
     */
    std::optional<WindowRates> rates;
};

/**
 *  Splits one thread's activity into consecutive windows of equal length.
 *
 *  @param      samples     The thread's samples, sorted by timestamp.
 *  @param      migrations  The thread's migrations, sorted by timestamp.
 *  @param      start_ns    Start of the first window.
 *  @param      end_ns      End of the last window.
 *  @param      window_ns   Length of each window.
 *  @return     The windows covering [start_ns, end_ns), or none if window_ns is zero
 *              or the range is empty.
 */
[[nodiscard]] auto buildThreadWindows(std::span<const core::PmuSample> samples,
                                      std::span<const TimedMigration> migrations,
                                      std::uint64_t start_ns, std::uint64_t end_ns,
                                      std::uint64_t window_ns) -> std::vector<ThreadWindow>;

}  // namespace threveal::analysis

#endif  // THREVEAL_ANALYSIS_TIME_WINDOWS_HPP_

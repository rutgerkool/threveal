/**
 *  @file       time_windows.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Implementation of the time-windowed view of a thread.
 */

#include "threveal/analysis/time_windows.hpp"

#include "threveal/analysis/pmu_interpolation.hpp"
#include "threveal/core/events.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace threveal::analysis
{

namespace
{

/**
 *  Counts one migration in the window's per-type tallies.
 */
void countMigration(ThreadWindow& window, core::MigrationType type) noexcept
{
    switch (type)
    {
        case core::MigrationType::kPToE:
            ++window.p_to_e_migrations;
            break;
        case core::MigrationType::kEToP:
            ++window.e_to_p_migrations;
            break;
        case core::MigrationType::kPToP:
            ++window.p_to_p_migrations;
            break;
        case core::MigrationType::kEToE:
            ++window.e_to_e_migrations;
            break;
        case core::MigrationType::kUnknown:
            break;
    }
}

/**
 *  Counts the migrations that fall in the window.
 */
void countMigrations(ThreadWindow& window, std::span<const TimedMigration> migrations) noexcept
{
    auto first =
        std::ranges::lower_bound(migrations, window.start_ns, {}, &TimedMigration::timestamp_ns);
    for (auto it = first; it != migrations.end() && it->timestamp_ns < window.end_ns; ++it)
    {
        countMigration(window, it->type);
    }
}

}  // namespace

auto buildThreadWindows(std::span<const core::PmuSample> samples,
                        std::span<const TimedMigration> migrations, std::uint64_t start_ns,
                        std::uint64_t end_ns, std::uint64_t window_ns) -> std::vector<ThreadWindow>
{
    std::vector<ThreadWindow> windows;
    if (window_ns == 0 || start_ns >= end_ns)
    {
        return windows;
    }

    windows.reserve(((end_ns - start_ns) + window_ns - 1) / window_ns);
    for (auto window_start = start_ns; window_start < end_ns;)
    {
        auto window_end = window_start + std::min(window_ns, end_ns - window_start);

        ThreadWindow window{};
        window.start_ns = window_start;
        window.end_ns = window_end;
        countMigrations(window, migrations);
        window.rates = ratesOver(samples, window_start, window_end);
        windows.push_back(window);

        window_start = window_end;
    }

    return windows;
}

}  // namespace threveal::analysis

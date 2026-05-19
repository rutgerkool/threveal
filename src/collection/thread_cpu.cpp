/**
 *  @file       thread_cpu.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Implementation of the thread CPU lookup.
 */

#include "threveal/collection/thread_cpu.hpp"

#include "threveal/core/types.hpp"

#include <charconv>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <system_error>

namespace threveal::collection
{

namespace
{

/**
 *  Position of the processor field among the fields after the comm field.
 *
 *  The processor is field 39 of the stat line and the first field after comm is field 3.
 */
constexpr std::ptrdiff_t kProcessorFieldIndex = 39 - 3;

}  // namespace

auto parseLastCpu(std::string_view stat_line) -> std::optional<core::CpuId>
{
    // The comm field may contain spaces and parentheses, so count from its closing one
    auto comm_end = stat_line.rfind(')');
    if (comm_end == std::string_view::npos)
    {
        return std::nullopt;
    }

    // Dropping the first field skips the empty token before the space after ')'
    auto fields = stat_line.substr(comm_end + 1) | std::views::split(' ') | std::views::drop(1);
    auto field_it = std::ranges::next(fields.begin(), kProcessorFieldIndex, fields.end());
    if (field_it == fields.end())
    {
        return std::nullopt;
    }

    std::string_view field((*field_it).begin(), (*field_it).end());
    core::CpuId cpu = 0;
    auto [end, error] = std::from_chars(field.data(), field.data() + field.size(), cpu);
    if (error != std::errc{} || end != field.data() + field.size())
    {
        return std::nullopt;
    }

    return cpu;
}

auto readLastCpu(pid_t tid) -> std::optional<core::CpuId>
{
    std::ifstream file("/proc/" + std::to_string(tid) + "/stat");
    std::string line;
    if (!std::getline(file, line))
    {
        return std::nullopt;
    }

    return parseLastCpu(line);
}

}  // namespace threveal::collection

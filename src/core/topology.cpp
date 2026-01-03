/**
 *  @file       topology.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Implementation of CPU topology detection.
 */

#include "threveal/core/topology.hpp"

#include "threveal/core/errors.hpp"
#include "threveal/core/types.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <expected>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

namespace threveal::core
{

namespace
{

/**
 *  Trims leading and trailing whitespace from a string view.
 *
 *  @param      str  The string view to trim.
 *  @return     A string view with whitespace removed from both ends.
 */
[[nodiscard]] constexpr auto trim(std::string_view str) noexcept -> std::string_view
{
    const char* whitespace = " \t\n\r";

    // Find first non-whitespace character
    auto start = str.find_first_not_of(whitespace);
    if (start == std::string_view::npos)
    {
        // String is all whitespace
        return {};
    }

    // Find last non-whitespace character
    auto end = str.find_last_not_of(whitespace);

    return str.substr(start, end - start + 1);
}

/**
 *  Parses a single unsigned integer from a string view.
 *
 *  Uses std::from_chars for efficient, locale-independent parsing.
 *
 *  @param      str  The string containing the number to parse.
 *  @return     The parsed CpuId on success, or TopologyError::kParseError
 *              if the string is not a valid unsigned integer.
 */
[[nodiscard]] auto parseNumber(std::string_view str) -> std::expected<CpuId, TopologyError>
{
    str = trim(str);

    if (str.empty())
    {
        return std::unexpected(TopologyError::kParseError);
    }

    CpuId value{};

    // ptr points to first unconsumed char, ec holds any error code
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);

    // Fail if parsing error or if input wasn't fully consumed
    if (ec != std::errc{} || ptr != str.data() + str.size())
    {
        return std::unexpected(TopologyError::kParseError);
    }

    return value;
}

/**
 *  Parses a single element which may be a number or a range.
 *
 *  Handles two formats:
 *  - Single number: "5" -> adds 5 to result
 *  - Range: "0-5" -> adds 0, 1, 2, 3, 4, 5 to result
 *
 *  @param      element  The element string to parse.
 *  @param      result   Vector to append parsed CPU IDs to.
 *  @return     Success or TopologyError::kParseError if format is invalid.
 */
[[nodiscard]] auto parseElement(std::string_view element, std::vector<CpuId>& result)
    -> std::expected<void, TopologyError>
{
    element = trim(element);

    if (element.empty())
    {
        return std::unexpected(TopologyError::kParseError);
    }

    // Check if element contains a dash (indicating a range)
    auto dash_pos = element.find('-');

    // Single number: parse and append, propagating any error via transform
    if (dash_pos == std::string_view::npos)
    {
        return parseNumber(element).transform(
            [&result](CpuId num)
            {
                result.push_back(num);
            });
    }

    // Range: split on dash and parse both ends
    auto start_str = element.substr(0, dash_pos);
    auto end_str = element.substr(dash_pos + 1);

    auto start = parseNumber(start_str);
    if (!start)
    {
        return std::unexpected(start.error());
    }

    auto end = parseNumber(end_str);
    if (!end)
    {
        return std::unexpected(end.error());
    }

    // Reject invalid ranges like "5-3"
    if (*start > *end)
    {
        return std::unexpected(TopologyError::kParseError);
    }

    // Expand range into individual CPU IDs
    for (CpuId cpu = *start; cpu <= *end; ++cpu)
    {
        result.push_back(cpu);
    }

    return {};
}

}  // namespace

TopologyMap::TopologyMap(std::span<const CpuId> p_cores, std::span<const CpuId> e_cores)
    : p_cores_(p_cores.begin(), p_cores.end()), e_cores_(e_cores.begin(), e_cores.end())
{
    buildLookupTable();
}

auto TopologyMap::getCoreType(CpuId cpu_id) const -> std::expected<CoreType, TopologyError>
{
    if (cpu_id >= cpu_to_type_.size())
    {
        return std::unexpected(TopologyError::kInvalidCpuId);
    }

    auto type = cpu_to_type_[cpu_id];

    // A CPU ID within bounds but marked Unknown means it wasn't in either list
    if (type == CoreType::kUnknown)
    {
        return std::unexpected(TopologyError::kInvalidCpuId);
    }

    return type;
}

auto TopologyMap::getPCores() const noexcept -> std::span<const CpuId>
{
    return p_cores_;
}

auto TopologyMap::getECores() const noexcept -> std::span<const CpuId>
{
    return e_cores_;
}

auto TopologyMap::totalCpuCount() const noexcept -> std::size_t
{
    return p_cores_.size() + e_cores_.size();
}

auto TopologyMap::isHybrid() const noexcept -> bool
{
    return !p_cores_.empty() && !e_cores_.empty();
}

auto TopologyMap::loadFromSysfs() -> std::expected<TopologyMap, TopologyError>
{
    // TODO: Implement sysfs file reading
    return std::unexpected(TopologyError::kSysfsNotFound);
}

void TopologyMap::buildLookupTable()
{
    // Find the maximum CPU ID to size the lookup table
    CpuId max_cpu = 0;

    for (CpuId cpu : p_cores_)
    {
        max_cpu = std::max(max_cpu, cpu);
    }
    for (CpuId cpu : e_cores_)
    {
        max_cpu = std::max(max_cpu, cpu);
    }

    // Resize lookup table to accommodate all CPU IDs (0 to max_cpu inclusive)
    // Unassigned entries default to kUnknown
    cpu_to_type_.resize(max_cpu + 1, CoreType::kUnknown);

    // Populate P-core entries
    for (CpuId cpu : p_cores_)
    {
        cpu_to_type_[cpu] = CoreType::kPCore;
    }

    // Populate E-core entries
    for (CpuId cpu : e_cores_)
    {
        cpu_to_type_[cpu] = CoreType::kECore;
    }
}

auto parseCpuList(std::string_view content) -> std::expected<std::vector<CpuId>, TopologyError>
{
    content = trim(content);

    if (content.empty())
    {
        return std::unexpected(TopologyError::kParseError);
    }

    std::vector<CpuId> result;
    std::size_t pos = 0;

    // Iterate through comma-separated elements
    while (pos < content.size())
    {
        // Find next comma delimiter
        auto comma_pos = content.find(',', pos);

        // Extract substring from pos to comma (or end of string)
        auto element = (comma_pos == std::string_view::npos) ? content.substr(pos)
                                                             : content.substr(pos, comma_pos - pos);

        if (auto parse_result = parseElement(element, result); !parse_result)
        {
            return std::unexpected(parse_result.error());
        }

        // No more commas means we've processed the last element
        if (comma_pos == std::string_view::npos)
        {
            break;
        }

        // Move past the comma to start of next element
        pos = comma_pos + 1;
    }

    return result;
}

}  // namespace threveal::core

/**
 *  @file       thread_discovery.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Implementation of thread discovery through /proc.
 */

#include "threveal/collection/thread_discovery.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <sys/types.h>
#include <system_error>
#include <vector>

namespace threveal::collection
{

auto listThreads(pid_t pid) -> std::optional<std::vector<pid_t>>
{
    std::error_code error;
    std::filesystem::directory_iterator entries("/proc/" + std::to_string(pid) + "/task", error);
    if (error)
    {
        return std::nullopt;
    }

    // The process can exit while its threads are listed, so iterate without exceptions
    std::vector<pid_t> tids;
    for (; entries != std::filesystem::directory_iterator{}; entries.increment(error))
    {
        auto name = entries->path().filename().string();
        const char* first = std::to_address(name.cbegin());
        const char* last = std::to_address(name.cend());
        pid_t tid = 0;
        auto [end, parse_error] = std::from_chars(first, last, tid);
        if (parse_error == std::errc{} && end == last)
        {
            tids.push_back(tid);
        }
    }
    if (error)
    {
        return std::nullopt;
    }

    std::ranges::sort(tids);
    return tids;
}

}  // namespace threveal::collection

/**
 *  @file       test_thread_discovery.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Unit tests for thread discovery.
 */

#include "threveal/collection/thread_discovery.hpp"

#include <algorithm>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

using threveal::collection::listThreads;

TEST_CASE("listThreads includes the calling thread", "[collection][thread_discovery]")
{
    auto tids = listThreads(getpid());

    REQUIRE(tids.has_value());
    REQUIRE(std::ranges::contains(*tids, gettid()));
}

TEST_CASE("listThreads includes every thread of the process", "[collection][thread_discovery]")
{
    std::atomic<pid_t> other_tid{0};
    std::atomic<bool> done{false};
    std::jthread other(
        [&]
        {
            other_tid = gettid();
            while (!done)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    while (other_tid == 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    auto tids = listThreads(getpid());
    done = true;

    REQUIRE(tids.has_value());
    REQUIRE(std::ranges::contains(*tids, gettid()));
    REQUIRE(std::ranges::contains(*tids, other_tid.load()));
    REQUIRE(std::ranges::is_sorted(*tids));
}

TEST_CASE("listThreads returns nullopt for a process that does not exist",
          "[collection][thread_discovery]")
{
    REQUIRE_FALSE(listThreads(999'999'999).has_value());
}

/**
 *  @file       test_migration_tracker.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Unit tests for MigrationTracker.
 *
 *  Note: eBPF operations require CAP_BPF or root privileges.
 *  Tests that require privileges will be skipped if permissions are insufficient.
 */

#include "threveal/collection/migration_tracker.hpp"
#include "threveal/core/events.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unistd.h>
#include <utility>
#include <vector>

#include "pmu_test_support.hpp"

using threveal::collection::EbpfError;
using threveal::collection::MigrationCallback;
using threveal::collection::MigrationTracker;
using threveal::core::MigrationEvent;
using threveal::test::ScopedCpuPin;

namespace
{

auto hasEbpfPrivileges() -> bool
{
    return geteuid() == 0;
}

class EventCollector
{
  public:
    void addEvent(const MigrationEvent& event)
    {
        std::lock_guard lock(mutex_);
        events_.push_back(event);
    }

    [[nodiscard]] auto events() const -> std::vector<MigrationEvent>
    {
        std::lock_guard lock(mutex_);
        return events_;
    }

    [[nodiscard]] auto count() const -> std::size_t
    {
        std::lock_guard lock(mutex_);
        return events_.size();
    }

  private:
    mutable std::mutex mutex_;
    std::vector<MigrationEvent> events_;
};

}  // namespace

TEST_CASE("MigrationTracker creation requires privileges", "[collection][MigrationTracker]")
{
    EventCollector collector;
    auto callback = [&collector](const MigrationEvent& event)
    {
        collector.addEvent(event);
    };

    auto tracker = MigrationTracker::create(callback);

    if (!hasEbpfPrivileges())
    {
        REQUIRE_FALSE(tracker.has_value());
        REQUIRE((tracker.error() == EbpfError::kPermissionDenied ||
                 tracker.error() == EbpfError::kLoadFailed));
        return;
    }

    REQUIRE(tracker.has_value());
    REQUIRE_FALSE(tracker->isRunning());
    REQUIRE(tracker->eventCount() == 0);
}

TEST_CASE("MigrationTracker rejects null callback", "[collection][MigrationTracker]")
{
    MigrationCallback null_callback;
    auto tracker = MigrationTracker::create(null_callback);

    REQUIRE_FALSE(tracker.has_value());
    REQUIRE(tracker.error() == EbpfError::kInvalidState);
}

TEST_CASE("MigrationTracker start and stop", "[collection][MigrationTracker]")
{
    if (!hasEbpfPrivileges())
    {
        SKIP("eBPF operations require root privileges");
    }

    EventCollector collector;
    auto tracker = MigrationTracker::create(
        [&collector](const MigrationEvent& event)
        {
            collector.addEvent(event);
        });
    REQUIRE(tracker.has_value());

    SECTION("start succeeds")
    {
        REQUIRE(tracker->start().has_value());
        REQUIRE(tracker->isRunning());
        tracker->stop();
        REQUIRE_FALSE(tracker->isRunning());
    }

    SECTION("stop is idempotent")
    {
        REQUIRE(tracker->start().has_value());
        tracker->stop();
        tracker->stop();
        REQUIRE_FALSE(tracker->isRunning());
    }

    SECTION("double start is idempotent")
    {
        REQUIRE(tracker->start().has_value());
        REQUIRE(tracker->start().has_value());
        REQUIRE(tracker->isRunning());
        tracker->stop();
    }
}

TEST_CASE("MigrationTracker move semantics", "[collection][MigrationTracker]")
{
    if (!hasEbpfPrivileges())
    {
        SKIP("eBPF operations require root privileges");
    }

    EventCollector collector;
    auto tracker1 = MigrationTracker::create(
        [&collector](const MigrationEvent& event)
        {
            collector.addEvent(event);
        });
    REQUIRE(tracker1.has_value());

    SECTION("move construction")
    {
        REQUIRE(tracker1->start().has_value());

        MigrationTracker tracker2 = std::move(*tracker1);
        REQUIRE(tracker2.isRunning());
        tracker2.stop();
    }

    SECTION("move assignment")
    {
        auto tracker2 = MigrationTracker::create(
            [&collector](const MigrationEvent& event)
            {
                collector.addEvent(event);
            });
        REQUIRE(tracker2.has_value());

        REQUIRE(tracker1->start().has_value());
        *tracker2 = std::move(*tracker1);
        REQUIRE(tracker2->isRunning());
        tracker2->stop();
    }
}

TEST_CASE("MigrationTracker setTargetPid", "[collection][MigrationTracker]")
{
    if (!hasEbpfPrivileges())
    {
        SKIP("eBPF operations require root privileges");
    }

    EventCollector collector;
    auto tracker = MigrationTracker::create(
        [&collector](const MigrationEvent& event)
        {
            collector.addEvent(event);
        });
    REQUIRE(tracker.has_value());

    SECTION("set specific PID")
    {
        REQUIRE(tracker->setTargetPid(getpid()).has_value());
    }

    SECTION("clear filter (capture all)")
    {
        REQUIRE(tracker->setTargetPid(std::nullopt).has_value());
    }
}

TEST_CASE("MigrationTracker poll returns without error", "[collection][MigrationTracker]")
{
    if (!hasEbpfPrivileges())
    {
        SKIP("eBPF operations require root privileges");
    }

    EventCollector collector;
    auto tracker = MigrationTracker::create(
        [&collector](const MigrationEvent& event)
        {
            collector.addEvent(event);
        });
    REQUIRE(tracker.has_value());

    REQUIRE(tracker->start().has_value());

    int result = tracker->poll(std::chrono::milliseconds(10));
    REQUIRE(result >= 0);

    tracker->stop();
}

TEST_CASE("MigrationTracker delivers migrations of the target process",
          "[collection][MigrationTracker]")
{
    if (!hasEbpfPrivileges())
    {
        SKIP("eBPF operations require root privileges");
    }
    if (sysconf(_SC_NPROCESSORS_ONLN) < 2)
    {
        SKIP("Requires at least two CPUs");
    }

    ScopedCpuPin start_pin(0);
    REQUIRE(start_pin.pinned());

    EventCollector collector;
    auto tracker = MigrationTracker::create(
        [&collector](const MigrationEvent& event)
        {
            collector.addEvent(event);
        });
    REQUIRE(tracker.has_value());
    REQUIRE(tracker->setTargetPid(getpid()).has_value());
    REQUIRE(tracker->start().has_value());

    {
        ScopedCpuPin move_pin(1);
        REQUIRE(move_pin.pinned());
    }

    auto has_own_migration = [&]
    {
        for (const auto& event : collector.events())
        {
            if (event.tid == static_cast<std::uint32_t>(gettid()) && event.dst_cpu == 1)
            {
                return true;
            }
        }
        return false;
    };

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!has_own_migration() && std::chrono::steady_clock::now() < deadline)
    {
        REQUIRE(tracker->poll(std::chrono::milliseconds(10)) >= 0);
    }

    REQUIRE(has_own_migration());
    REQUIRE(tracker->eventCount() == collector.count());
}

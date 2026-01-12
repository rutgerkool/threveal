/**
 *  @file       test_pmu_sampler.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Unit tests for PmuSampler.
 *
 *  Note: Many PMU operations require CAP_PERFMON or perf_event_paranoid <= 1.
 *  Tests that require privileges will be skipped if permissions are insufficient.
 */

#include "threveal/collection/pmu_sampler.hpp"
#include "threveal/core/errors.hpp"
#include "threveal/core/events.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

using threveal::collection::PmuSampler;
using threveal::core::PmuError;
using threveal::core::PmuSample;

namespace
{

/**
 *  Checks if PMU access is permitted on this system.
 */
auto hasPmuAccess() -> bool
{
    std::ifstream file("/proc/sys/kernel/perf_event_paranoid");
    if (!file)
    {
        return false;
    }

    int level = 0;
    file >> level;

    return level <= 1;
}

/**
 *  Thread-safe sample collector for testing.
 */
class SampleCollector
{
  public:
    void addSample(const PmuSample& sample)
    {
        std::lock_guard lock(mutex_);
        samples_.push_back(sample);
    }

    [[nodiscard]] auto samples() const -> std::vector<PmuSample>
    {
        std::lock_guard lock(mutex_);
        return samples_;
    }

    [[nodiscard]] auto count() const -> std::size_t
    {
        std::lock_guard lock(mutex_);
        return samples_.size();
    }

    void clear()
    {
        std::lock_guard lock(mutex_);
        samples_.clear();
    }

  private:
    mutable std::mutex mutex_;
    std::vector<PmuSample> samples_;
};

}  // namespace

TEST_CASE("PmuSampler creation requires permissions", "[collection][PmuSampler]")
{
    SampleCollector collector;
    auto callback = [&collector](const PmuSample& sample)
    {
        collector.addSample(sample);
    };

    auto sampler = PmuSampler::create(0, callback);

    if (!hasPmuAccess())
    {
        REQUIRE_FALSE(sampler.has_value());
        REQUIRE(sampler.error() == PmuError::kPermissionDenied);
    }
    else
    {
        // May still fail if LLC events not supported
        if (sampler.has_value())
        {
            REQUIRE_FALSE(sampler->isRunning());
            REQUIRE(sampler->sampleCount() == 0);
        }
        else
        {
            REQUIRE((sampler.error() == PmuError::kEventNotSupported ||
                     sampler.error() == PmuError::kTooManyEvents));
        }
    }
}

TEST_CASE("PmuSampler rejects null callback", "[collection][PmuSampler]")
{
    PmuSampler::SampleCallback null_callback;
    auto sampler = PmuSampler::create(0, null_callback);

    REQUIRE_FALSE(sampler.has_value());
    REQUIRE(sampler.error() == PmuError::kInvalidState);
}

TEST_CASE("PmuSampler enforces minimum interval", "[collection][PmuSampler]")
{
    if (!hasPmuAccess())
    {
        SKIP("PMU access not permitted");
    }

    SampleCollector collector;
    auto callback = [&collector](const PmuSample& sample)
    {
        collector.addSample(sample);
    };

    // Request interval below minimum
    auto sampler = PmuSampler::create(0, callback, std::chrono::microseconds(10));

    if (!sampler.has_value())
    {
        SKIP("PMU group creation failed");
    }

    // Should be clamped to minimum
    REQUIRE(sampler->interval() >= PmuSampler::kMinInterval);
}

TEST_CASE("PmuSampler default interval", "[collection][PmuSampler]")
{
    if (!hasPmuAccess())
    {
        SKIP("PMU access not permitted");
    }

    SampleCollector collector;
    auto callback = [&collector](const PmuSample& sample)
    {
        collector.addSample(sample);
    };

    auto sampler = PmuSampler::create(0, callback);

    if (!sampler.has_value())
    {
        SKIP("PMU group creation failed");
    }

    REQUIRE(sampler->interval() == PmuSampler::kDefaultInterval);
}

TEST_CASE("PmuSampler start and stop", "[collection][PmuSampler]")
{
    if (!hasPmuAccess())
    {
        SKIP("PMU access not permitted");
    }

    SampleCollector collector;
    auto callback = [&collector](const PmuSample& sample)
    {
        collector.addSample(sample);
    };

    auto sampler = PmuSampler::create(0, callback, std::chrono::milliseconds(5));

    if (!sampler.has_value())
    {
        SKIP("PMU group creation failed");
    }

    SECTION("starts successfully")
    {
        auto result = sampler->start();
        REQUIRE(result.has_value());
        REQUIRE(sampler->isRunning());

        sampler->stop();
        REQUIRE_FALSE(sampler->isRunning());
    }

    SECTION("stop is idempotent")
    {
        auto result = sampler->start();
        REQUIRE(result.has_value());

        sampler->stop();
        sampler->stop();  // Should not crash
        REQUIRE_FALSE(sampler->isRunning());
    }

    SECTION("cannot start twice")
    {
        auto result1 = sampler->start();
        REQUIRE(result1.has_value());

        auto result2 = sampler->start();
        REQUIRE_FALSE(result2.has_value());
        REQUIRE(result2.error() == PmuError::kInvalidState);

        sampler->stop();
    }
}

TEST_CASE("PmuSampler collects samples", "[collection][PmuSampler]")
{
    if (!hasPmuAccess())
    {
        SKIP("PMU access not permitted");
    }

    SampleCollector collector;
    auto callback = [&collector](const PmuSample& sample)
    {
        collector.addSample(sample);
    };

    // Use 2ms interval for faster test
    auto sampler = PmuSampler::create(0, callback, std::chrono::milliseconds(2));

    if (!sampler.has_value())
    {
        SKIP("PMU group creation failed");
    }

    auto start_result = sampler->start();
    REQUIRE(start_result.has_value());

    // Do some work while sampling
    volatile std::uint64_t sum = 0;
    auto start_time = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start_time < std::chrono::milliseconds(50))
    {
        for (std::uint64_t i = 0; i < 10000; ++i)
        {
            sum += i;
        }
    }
    (void)sum;

    sampler->stop();

    // Should have collected some samples (50ms / 2ms = ~25 samples)
    REQUIRE(collector.count() > 0);
    REQUIRE(sampler->sampleCount() == collector.count());

    // Verify sample contents
    auto samples = collector.samples();
    for (const auto& sample : samples)
    {
        REQUIRE(sample.timestamp_ns > 0);
        REQUIRE(sample.cycles > 0);
        REQUIRE(sample.instructions > 0);
    }
}

TEST_CASE("PmuSampler samples have increasing timestamps", "[collection][PmuSampler]")
{
    if (!hasPmuAccess())
    {
        SKIP("PMU access not permitted");
    }

    SampleCollector collector;
    auto callback = [&collector](const PmuSample& sample)
    {
        collector.addSample(sample);
    };

    auto sampler = PmuSampler::create(0, callback, std::chrono::milliseconds(2));

    if (!sampler.has_value())
    {
        SKIP("PMU group creation failed");
    }

    auto start_result = sampler->start();
    REQUIRE(start_result.has_value());

    // Let it run for a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    sampler->stop();

    auto samples = collector.samples();
    REQUIRE(samples.size() >= 2);

    // Verify timestamps are monotonically increasing
    for (std::size_t i = 1; i < samples.size(); ++i)
    {
        REQUIRE(samples[i].timestamp_ns > samples[i - 1].timestamp_ns);
    }
}

TEST_CASE("PmuSampler move semantics", "[collection][PmuSampler]")
{
    if (!hasPmuAccess())
    {
        SKIP("PMU access not permitted");
    }

    SampleCollector collector;
    auto callback = [&collector](const PmuSample& sample)
    {
        collector.addSample(sample);
    };

    auto sampler1 = PmuSampler::create(0, callback, std::chrono::milliseconds(5));

    if (!sampler1.has_value())
    {
        SKIP("PMU group creation failed");
    }

    SECTION("move construction")
    {
        auto start_result = sampler1->start();
        REQUIRE(start_result.has_value());

        PmuSampler sampler2 = std::move(*sampler1);
        REQUIRE(sampler2.isRunning());

        sampler2.stop();
        REQUIRE_FALSE(sampler2.isRunning());
    }

    SECTION("move assignment")
    {
        auto sampler2 = PmuSampler::create(0, callback, std::chrono::milliseconds(5));

        if (!sampler2.has_value())
        {
            SKIP("PMU group creation failed");
        }

        auto start_result = sampler1->start();
        REQUIRE(start_result.has_value());

        *sampler2 = std::move(*sampler1);
        REQUIRE(sampler2->isRunning());

        sampler2->stop();
    }
}

TEST_CASE("PmuSampler destructor stops sampling", "[collection][PmuSampler]")
{
    if (!hasPmuAccess())
    {
        SKIP("PMU access not permitted");
    }

    SampleCollector collector;
    auto callback = [&collector](const PmuSample& sample)
    {
        collector.addSample(sample);
    };

    {
        auto sampler = PmuSampler::create(0, callback, std::chrono::milliseconds(2));

        if (!sampler.has_value())
        {
            SKIP("PMU group creation failed");
        }

        auto start_result = sampler->start();
        REQUIRE(start_result.has_value());
        REQUIRE(sampler->isRunning());

        // Let it collect a few samples
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        // Destructor should stop cleanly
    }

    // If we get here without hanging, the destructor worked correctly
    REQUIRE(collector.count() > 0);
}

TEST_CASE("PmuSampler targetTid returns configured TID", "[collection][PmuSampler]")
{
    if (!hasPmuAccess())
    {
        SKIP("PMU access not permitted");
    }

    SampleCollector collector;
    auto callback = [&collector](const PmuSample& sample)
    {
        collector.addSample(sample);
    };

    auto sampler = PmuSampler::create(0, callback);

    if (!sampler.has_value())
    {
        SKIP("PMU group creation failed");
    }

    // TID 0 means "self" - the actual TID should still be 0 in the sampler
    REQUIRE(sampler->targetTid() == 0);
}

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
#include "threveal/core/topology.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include "pmu_test_support.hpp"

using threveal::collection::PmuSampler;
using threveal::core::PmuError;
using threveal::core::PmuSample;
using threveal::core::TopologyMap;
using threveal::test::hasPmuAccess;
using threveal::test::ScopedCpuPin;

namespace
{

/**
 *  Returns the current time on the clock PmuSampler uses for its timestamps.
 */
auto monotonicNowNs() -> std::uint64_t
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ULL) +
           static_cast<std::uint64_t>(ts.tv_nsec);
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

    auto sampler = PmuSampler::create(0, callback, std::chrono::microseconds(10));

    if (!sampler.has_value())
    {
        SKIP("PMU group creation failed");
    }

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

    auto sampler = PmuSampler::create(0, callback, std::chrono::milliseconds(2));

    if (!sampler.has_value())
    {
        SKIP("PMU group creation failed");
    }

    auto start_result = sampler->start();
    REQUIRE(start_result.has_value());

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

    REQUIRE(collector.count() > 0);
    REQUIRE(sampler->sampleCount() == collector.count());

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

    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    sampler->stop();

    auto samples = collector.samples();
    REQUIRE(samples.size() >= 2);

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

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    REQUIRE(collector.count() > 0);
}

TEST_CASE("PmuSampler resolves tid 0 to the calling thread", "[collection][PmuSampler]")
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

    REQUIRE(sampler->targetTid() == gettid());
}

TEST_CASE("PmuSampler samples carry the resolved tid", "[collection][PmuSampler]")
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

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    sampler->stop();

    auto samples = collector.samples();
    REQUIRE_FALSE(samples.empty());

    for (const auto& sample : samples)
    {
        REQUIRE(sample.tid == static_cast<std::uint32_t>(gettid()));
    }
}

TEST_CASE("PmuSampler records the CPU of the target thread", "[collection][PmuSampler]")
{
    if (!hasPmuAccess())
    {
        SKIP("PMU access not permitted");
    }

    auto topology = TopologyMap::loadFromSysfs();
    if (!topology.has_value() || !topology->isHybrid())
    {
        SKIP("Requires a hybrid CPU");
    }

    auto p_core = topology->getPCores().front();
    auto e_core = topology->getECores().front();

    ScopedCpuPin sampler_pin(p_core);
    REQUIRE(sampler_pin.pinned());

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

    std::uint64_t pinned_at_ns = 0;
    {
        ScopedCpuPin target_pin(e_core);
        REQUIRE(target_pin.pinned());

        pinned_at_ns = monotonicNowNs();

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        sampler->stop();
    }

    auto samples = collector.samples();
    std::erase_if(samples,
                  [pinned_at_ns](const PmuSample& sample)
                  {
                      return sample.timestamp_ns < pinned_at_ns;
                  });
    REQUIRE_FALSE(samples.empty());

    for (const auto& sample : samples)
    {
        REQUIRE(sample.cpu_id == e_core);
    }
}

TEST_CASE("PmuSampler reports counts per interval", "[collection][PmuSampler]")
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

    volatile std::uint64_t sum = 0;
    auto busy_start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - busy_start < std::chrono::milliseconds(20))
    {
        for (std::uint64_t i = 0; i < 1000; ++i)
        {
            sum += i;
        }
    }
    (void)sum;

    // A sleeping thread retires no user-space instructions
    constexpr std::uint64_t kSettleNs = 10'000'000;
    std::uint64_t idle_from_ns = monotonicNowNs() + kSettleNs;
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    std::uint64_t idle_until_ns = monotonicNowNs();

    sampler->stop();

    auto samples = collector.samples();
    std::erase_if(samples,
                  [idle_from_ns, idle_until_ns](const PmuSample& sample)
                  {
                      return sample.timestamp_ns < idle_from_ns ||
                             sample.timestamp_ns > idle_until_ns;
                  });
    REQUIRE_FALSE(samples.empty());

    constexpr std::uint64_t kIdleInstructionLimit = 1000;
    for (const auto& sample : samples)
    {
        REQUIRE(sample.instructions < kIdleInstructionLimit);
    }
}

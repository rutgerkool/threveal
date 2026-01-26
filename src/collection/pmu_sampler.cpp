/**
 *  @file       pmu_sampler.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Implementation of periodic PMU sampling.
 */

#include "threveal/collection/pmu_sampler.hpp"

#include "threveal/collection/pmu_group.hpp"
#include "threveal/core/errors.hpp"
#include "threveal/core/events.hpp"
#include "threveal/core/types.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <sched.h>
#include <stop_token>
#include <sys/types.h>
#include <thread>
#include <time.h>
#include <utility>

namespace threveal::collection
{

namespace
{

/**
 *  Gets the current timestamp in nanoseconds since boot.
 *
 *  @return     Nanoseconds since boot.
 */
auto getTimestampNs() noexcept -> std::uint64_t
{
    timespec ts{};

    // CLOCK_MONOTONIC matches bpf_ktime_get_ns() used in eBPF programs
    clock_gettime(CLOCK_MONOTONIC, &ts);

    // Convert to nanoseconds, handling potential overflow for long uptimes
    constexpr std::uint64_t kNsPerSecond = 1'000'000'000ULL;
    return (static_cast<std::uint64_t>(ts.tv_sec) * kNsPerSecond) +
           static_cast<std::uint64_t>(ts.tv_nsec);
}

/**
 *  Gets the CPU ID where the calling thread is currently running.
 *
 *  @return     The current CPU ID, or 0 if detection fails.
 */
auto getCurrentCpu() noexcept -> core::CpuId
{
    // sched_getcpu() returns the CPU number of the calling thread
    int cpu = sched_getcpu();
    if (cpu < 0)
    {
        return 0;
    }
    return static_cast<core::CpuId>(cpu);
}

}  // namespace

PmuSampler::PmuSampler(pid_t tid, PmuGroup group, SampleCallback callback,
                       std::chrono::microseconds interval) noexcept
    : tid_(tid), group_(std::move(group)), callback_(std::move(callback)), interval_(interval)
{
}

PmuSampler::~PmuSampler()
{
    // Ensure sampling thread is stopped before destroying members
    stop();
}

PmuSampler::PmuSampler(PmuSampler&& other) noexcept
    : tid_(other.tid_),
      group_(std::move(other.group_)),
      callback_(std::move(other.callback_)),
      interval_(other.interval_),
      sampling_thread_(std::move(other.sampling_thread_)),
      sample_count_(other.sample_count_.load()),
      running_(other.running_.load())
{
    // Invalidate source
    other.tid_ = 0;
    other.sample_count_ = 0;
    other.running_ = false;
}

auto PmuSampler::operator=(PmuSampler&& other) noexcept -> PmuSampler&
{
    if (this != &other)
    {
        // Stop our current sampling thread before taking ownership
        stop();

        tid_ = other.tid_;
        group_ = std::move(other.group_);
        callback_ = std::move(other.callback_);
        interval_ = other.interval_;
        sampling_thread_ = std::move(other.sampling_thread_);
        sample_count_ = other.sample_count_.load();
        running_ = other.running_.load();

        // Invalidate source
        other.tid_ = 0;
        other.sample_count_ = 0;
        other.running_ = false;
    }
    return *this;
}

auto PmuSampler::create(pid_t tid, SampleCallback callback, std::chrono::microseconds interval)
    -> std::expected<PmuSampler, core::PmuError>
{
    // Validate callback is not empty
    if (!callback)
    {
        return std::unexpected(core::PmuError::kInvalidState);
    }

    // Enforce minimum interval to prevent excessive CPU usage
    if (interval < kMinInterval)
    {
        interval = kMinInterval;
    }

    // Create PMU counter group for the target thread
    auto group = PmuGroup::create(tid);
    if (!group)
    {
        return std::unexpected(group.error());
    }

    return PmuSampler{tid, std::move(*group), std::move(callback), interval};
}

auto PmuSampler::start() -> std::expected<void, core::PmuError>
{
    // Check if already running
    if (running_.load(std::memory_order_acquire))
    {
        return std::unexpected(core::PmuError::kInvalidState);
    }

    // Reset counters for fresh measurement
    auto reset_result = group_.reset();
    if (!reset_result)
    {
        return std::unexpected(reset_result.error());
    }

    // Enable PMU counters
    auto enable_result = group_.enable();
    if (!enable_result)
    {
        return std::unexpected(enable_result.error());
    }

    // Reset sample count for this session
    sample_count_.store(0, std::memory_order_relaxed);

    // Mark as running before starting thread
    running_.store(true, std::memory_order_release);

    // Start sampling thread with stop token support
    sampling_thread_ = std::jthread(
        [this](const std::stop_token& stop_token)
        {
            samplingLoop(stop_token);
        });

    return {};
}

void PmuSampler::stop() noexcept
{
    // Check if actually running
    if (!running_.load(std::memory_order_acquire))
    {
        return;
    }

    // Request thread to stop
    if (sampling_thread_.joinable())
    {
        sampling_thread_.request_stop();
        sampling_thread_.join();
    }

    // Disable PMU counters, ignoring errors during shutdown
    // as there's nothing we can do about them at this point
    auto disable_result = group_.disable();
    (void)disable_result;

    // Mark as stopped
    running_.store(false, std::memory_order_release);
}

auto PmuSampler::isRunning() const noexcept -> bool
{
    return running_.load(std::memory_order_acquire);
}

auto PmuSampler::sampleCount() const noexcept -> std::uint64_t
{
    return sample_count_.load(std::memory_order_relaxed);
}

auto PmuSampler::interval() const noexcept -> std::chrono::microseconds
{
    return interval_;
}

auto PmuSampler::targetTid() const noexcept -> pid_t
{
    return tid_;
}

void PmuSampler::samplingLoop(const std::stop_token& stop_token)
{
    // Sampling loop runs until stop is requested
    while (!stop_token.stop_requested())
    {
        if (collectSample())
        {
            sample_count_.fetch_add(1, std::memory_order_relaxed);
        }

        // Sleep for the configured interval
        std::this_thread::sleep_for(interval_);
    }
}

auto PmuSampler::collectSample() -> bool
{
    // Read PMU counters atomically
    auto reading = group_.read();
    if (!reading)
    {
        // Counter read failed - skip this sample
        return false;
    }

    // Get timestamp as close to the PMU read as possible
    auto timestamp = getTimestampNs();

    // Get current CPU for the target thread
    auto cpu_id = getCurrentCpu();

    // Build the PmuSample structure
    core::PmuSample sample{
        .timestamp_ns = timestamp,
        .tid = static_cast<std::uint32_t>(tid_),
        .cpu_id = cpu_id,
        .instructions = reading->instructions,
        .cycles = reading->cycles,
        .llc_misses = reading->llc_load_misses,
        .llc_references = reading->llc_loads,
        .branch_misses = reading->branch_misses,
    };

    // Deliver sample via callback
    callback_(sample);

    return true;
}

}  // namespace threveal::collection

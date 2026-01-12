/**
 *  @file       pmu_sampler.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Periodic PMU sampling for migration impact analysis.
 *
 *  Provides a high-frequency sampler that collects hardware performance
 *  counter snapshots at configurable intervals, enabling correlation
 *  between PMU metrics and migration events.
 */

#ifndef THREVEAL_COLLECTION_PMU_SAMPLER_HPP_
#define THREVEAL_COLLECTION_PMU_SAMPLER_HPP_

#include "threveal/collection/pmu_group.hpp"
#include "threveal/core/errors.hpp"
#include "threveal/core/events.hpp"
#include "threveal/core/types.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <stop_token>
#include <sys/types.h>
#include <thread>

namespace threveal::collection
{

/**
 *  Periodic sampler for hardware performance counters.
 *
 *  PmuSampler creates a background thread that periodically reads PMU counters
 *  and delivers samples via a callback. This enables correlation between
 *  performance metrics and scheduler migration events.
 *
 *  The sampler uses std::jthread with cooperative cancellation for clean
 *  shutdown. Samples include timestamps synchronized with migration events.
 *
 *  This class is move-only; the sampling thread cannot be safely copied.
 *
 *  Example usage:
 *  @code
 *      auto sampler = PmuSampler::create(tid, [&store](const auto& sample) {
 *          store.addPmuSample(sample);
 *      });
 *      if (!sampler) {
 *          // Handle error
 *      }
 *      sampler->start();
 *      // ... workload runs ...
 *      sampler->stop();  // Or let destructor handle it
 *  @endcode
 */
class PmuSampler
{
  public:
    /**
     *  Callback type for delivering PMU samples.
     *
     *  The callback is invoked from the sampling thread. Implementations
     *  must be thread-safe and should complete quickly to avoid affecting
     *  sample timing accuracy.
     */
    using SampleCallback = std::function<void(const core::PmuSample&)>;

    /**
     *  Default sampling interval (1 millisecond).
     *
     *  This provides a good balance between timing accuracy and overhead.
     *  Higher frequencies improve migration-PMU correlation but increase
     *  CPU usage from the sampler thread.
     */
    static constexpr auto kDefaultInterval = std::chrono::milliseconds(1);

    /**
     *  Minimum allowed sampling interval (100 microseconds).
     *
     *  Intervals below this may cause excessive overhead and timing jitter.
     */
    static constexpr auto kMinInterval = std::chrono::microseconds(100);

    /**
     *  Creates a new PMU sampler for the specified thread.
     *
     *  Opens a PMU counter group for the target thread but does not start
     *  sampling. Call start() to begin collecting samples.
     *
     *  @param      tid       Thread ID to monitor (0 for calling thread).
     *  @param      callback  Function to receive PMU samples.
     *  @param      interval  Time between samples (default: 1ms).
     *  @return     A PmuSampler on success, or PmuError on failure.
     */
    [[nodiscard]] static auto create(pid_t tid, SampleCallback callback,
                                     std::chrono::microseconds interval = kDefaultInterval)
        -> std::expected<PmuSampler, core::PmuError>;

    /**
     *  Destroys the sampler, stopping sampling if running.
     *
     *  Blocks until the sampling thread has terminated.
     */
    ~PmuSampler();

    /**
     *  Move constructor.
     *
     *  @param      other  Sampler to move from (will be invalidated).
     */
    PmuSampler(PmuSampler&& other) noexcept;

    /**
     *  Move assignment operator.
     *
     *  Stops any existing sampling before taking ownership.
     *
     *  @param      other  Sampler to move from (will be invalidated).
     *  @return     Reference to this sampler.
     */
    auto operator=(PmuSampler&& other) noexcept -> PmuSampler&;

    // Non-copyable
    PmuSampler(const PmuSampler&) = delete;
    auto operator=(const PmuSampler&) -> PmuSampler& = delete;

    /**
     *  Starts periodic sampling.
     *
     *  Creates a background thread that reads PMU counters at the configured
     *  interval and invokes the callback with each sample.
     *
     *  @return     Success, or PmuError if already running or PMU setup fails.
     */
    [[nodiscard]] auto start() -> std::expected<void, core::PmuError>;

    /**
     *  Stops periodic sampling.
     *
     *  Signals the sampling thread to stop and waits for it to terminate.
     *  This is a no-op if sampling is not currently running.
     */
    void stop() noexcept;

    /**
     *  Checks if sampling is currently active.
     *
     *  @return     True if the sampling thread is running.
     */
    [[nodiscard]] auto isRunning() const noexcept -> bool;

    /**
     *  Returns the number of samples collected since start().
     *
     *  @return     The total sample count.
     */
    [[nodiscard]] auto sampleCount() const noexcept -> std::uint64_t;

    /**
     *  Returns the configured sampling interval.
     *
     *  @return     The interval between samples.
     */
    [[nodiscard]] auto interval() const noexcept -> std::chrono::microseconds;

    /**
     *  Returns the target thread ID.
     *
     *  @return     The thread ID being monitored.
     */
    [[nodiscard]] auto targetTid() const noexcept -> pid_t;

  private:
    /**
     *  Private constructor - use create() factory method.
     *
     *  @param      tid       Thread ID being monitored.
     *  @param      group     PMU counter group for the target thread.
     *  @param      callback  Function to receive samples.
     *  @param      interval  Time between samples.
     */
    PmuSampler(pid_t tid, PmuGroup group, SampleCallback callback,
               std::chrono::microseconds interval) noexcept;

    /**
     *  Sampling thread entry point.
     *
     *  Runs the sampling loop until stop is requested via the stop token.
     *
     *  @param      stop_token  Token for cooperative cancellation.
     */
    void samplingLoop(const std::stop_token& stop_token);

    /**
     *  Collects a single PMU sample.
     *
     *  Reads the PMU counters, creates a PmuSample with the current
     *  timestamp, and invokes the callback.
     *
     *  @return     True if sample was collected successfully.
     */
    auto collectSample() -> bool;

    pid_t tid_;
    PmuGroup group_;
    SampleCallback callback_;
    std::chrono::microseconds interval_;

    std::jthread sampling_thread_;
    std::atomic<std::uint64_t> sample_count_{0};
    std::atomic<bool> running_{false};
};

}  // namespace threveal::collection

#endif  // THREVEAL_COLLECTION_PMU_SAMPLER_HPP_

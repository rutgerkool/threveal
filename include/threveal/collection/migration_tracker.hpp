/**
 *  @file       migration_tracker.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Migration event tracker using eBPF.
 *
 *  Provides high-level interface for capturing scheduler migration events
 *  via the eBPF tracepoint and delivering them to userspace.
 */

#ifndef THREVEAL_COLLECTION_MIGRATION_TRACKER_HPP_
#define THREVEAL_COLLECTION_MIGRATION_TRACKER_HPP_

#include "threveal/collection/ebpf_loader.hpp"
#include "threveal/core/events.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>

// Forward declaration for libbpf ring buffer
struct ring_buffer;

namespace threveal::collection
{

/**
 *  Callback type for delivering migration events.
 *
 *  The callback is invoked from poll() for each migration event.
 *  Implementations should complete quickly to avoid blocking event delivery.
 */
using MigrationCallback = std::function<void(const core::MigrationEvent&)>;

/**
 *  Tracks scheduler migration events using eBPF.
 *
 *  MigrationTracker combines EbpfLoader with a ring buffer consumer to
 *  provide a high-level interface for capturing and processing migration
 *  events. Events are delivered via callback or can be polled manually.
 *
 *  This class is move-only.
 *
 *  Example usage:
 *  @code
 *      auto tracker = MigrationTracker::create([&store](const auto& event) {
 *          store.addMigration(event);
 *      });
 *      if (!tracker) {
 *          // Handle error
 *      }
 *      tracker->start();
 *      // ... let it run ...
 *      tracker->poll(std::chrono::milliseconds(100));
 *      tracker->stop();
 *  @endcode
 */
class MigrationTracker
{
  public:
    /**
     *  Creates a new MigrationTracker.
     *
     *  Initializes the eBPF program and ring buffer consumer, but does not
     *  start tracking. Call start() to begin capturing events.
     *
     *  @param      callback  Function to receive migration events.
     *  @return     A MigrationTracker on success, or EbpfError on failure.
     */
    [[nodiscard]] static auto create(MigrationCallback callback)
        -> std::expected<MigrationTracker, EbpfError>;

    /**
     *  Destroys the tracker and releases all resources.
     */
    ~MigrationTracker();

    // Move-only semantics
    MigrationTracker(MigrationTracker&& other) noexcept;
    auto operator=(MigrationTracker&& other) noexcept -> MigrationTracker&;
    MigrationTracker(const MigrationTracker&) = delete;
    auto operator=(const MigrationTracker&) -> MigrationTracker& = delete;

    /**
     *  Starts capturing migration events.
     *
     *  Attaches the eBPF program to the tracepoint. After this call,
     *  events will accumulate in the ring buffer until poll() is called.
     *
     *  @return     Success or EbpfError on failure.
     */
    [[nodiscard]] auto start() -> std::expected<void, EbpfError>;

    /**
     *  Stops capturing migration events.
     *
     *  Detaches the eBPF program. Events already in the ring buffer
     *  can still be consumed via poll().
     */
    void stop() noexcept;

    /**
     *  Polls for pending migration events.
     *
     *  Consumes events from the ring buffer and invokes the callback
     *  for each one. Blocks for up to the specified timeout if no
     *  events are immediately available.
     *
     *  @param      timeout  Maximum time to wait for events.
     *  @return     Number of events processed, or -1 on error.
     */
    [[nodiscard]] auto poll(std::chrono::milliseconds timeout) -> int;

    /**
     *  Sets the target PID filter.
     *
     *  When set to a non-zero value, only migrations for the specified
     *  process (and its threads) will be captured.
     *
     *  @param      pid  Process ID to filter, or std::nullopt to capture all.
     *  @return     Success or EbpfError on failure.
     */
    [[nodiscard]] auto setTargetPid(std::optional<std::uint32_t> pid)
        -> std::expected<void, EbpfError>;

    /**
     *  Checks if tracking is currently active.
     */
    [[nodiscard]] auto isRunning() const noexcept -> bool;

    /**
     *  Returns the total number of events processed.
     */
    [[nodiscard]] auto eventCount() const noexcept -> std::uint64_t;

  private:
    MigrationTracker(EbpfLoader loader, ring_buffer* ring_buf, MigrationCallback callback) noexcept;

    /**
     *  Ring buffer callback invoked by libbpf.
     *
     *  This static function is registered with the ring buffer and
     *  dispatches events to the instance callback.
     */
    static auto ringBufferCallback(void* ctx, void* data, std::size_t size) -> int;

    EbpfLoader loader_;
    ring_buffer* ring_buf_;
    MigrationCallback callback_;
    std::atomic<std::uint64_t> event_count_{0};
    bool running_{false};
};

}  // namespace threveal::collection

#endif  // THREVEAL_COLLECTION_MIGRATION_TRACKER_HPP_

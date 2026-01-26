/**
 *  @file       migration_tracker.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Migration event tracker using eBPF.
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
 */
using MigrationCallback = std::function<void(const core::MigrationEvent&)>;

/**
 *  Tracks scheduler migration events using eBPF.
 */
class MigrationTracker
{
  public:
    /**
     *  Creates a new MigrationTracker.
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
     *  @return     Success or EbpfError on failure.
     */
    [[nodiscard]] auto start() -> std::expected<void, EbpfError>;

    /**
     *  Stops capturing migration events.
     */
    void stop() noexcept;

    /**
     *  Polls for pending migration events.
     *
     *  @param      timeout  Maximum time to wait for events.
     *  @return     Number of events processed, or -1 on error.
     */
    [[nodiscard]] auto poll(std::chrono::milliseconds timeout) -> int;

    /**
     *  Sets the target PID filter.
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

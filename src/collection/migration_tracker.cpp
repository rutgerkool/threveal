/**
 *  @file       migration_tracker.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Implementation of the MigrationTracker class.
 */

#include "threveal/collection/migration_tracker.hpp"

#include "threveal/collection/ebpf_loader.hpp"
#include "threveal/core/events.hpp"

#include <bpf/libbpf.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <functional>
#include <optional>
#include <utility>

// Shared BPF structures
#include "bpf_common.h"

namespace threveal::collection
{

MigrationTracker::MigrationTracker(EbpfLoader loader, ring_buffer* ring_buf,
                                   MigrationCallback callback) noexcept
    : loader_(std::move(loader)), ring_buf_(ring_buf), callback_(std::move(callback))
{
}

MigrationTracker::~MigrationTracker()
{
    if (running_)
    {
        stop();
    }

    if (ring_buf_ != nullptr)
    {
        ring_buffer__free(ring_buf_);
        ring_buf_ = nullptr;
    }
}

MigrationTracker::MigrationTracker(MigrationTracker&& other) noexcept
    : loader_(std::move(other.loader_)),
      ring_buf_(std::exchange(other.ring_buf_, nullptr)),
      callback_(std::move(other.callback_)),
      event_count_(other.event_count_.load()),
      running_(std::exchange(other.running_, false))
{
}

auto MigrationTracker::operator=(MigrationTracker&& other) noexcept -> MigrationTracker&
{
    if (this == &other)
    {
        return *this;
    }

    // Clean up current resources
    if (running_)
    {
        stop();
    }
    if (ring_buf_ != nullptr)
    {
        ring_buffer__free(ring_buf_);
    }

    // Take ownership
    loader_ = std::move(other.loader_);
    ring_buf_ = std::exchange(other.ring_buf_, nullptr);
    callback_ = std::move(other.callback_);
    event_count_ = other.event_count_.load();
    running_ = std::exchange(other.running_, false);

    return *this;
}

auto MigrationTracker::create(MigrationCallback callback)
    -> std::expected<MigrationTracker, EbpfError>
{
    if (!callback)
    {
        return std::unexpected(EbpfError::kInvalidState);
    }

    // Create and load the eBPF program
    auto loader = EbpfLoader::create();
    if (!loader)
    {
        return std::unexpected(loader.error());
    }

    // Get the ring buffer file descriptor
    int ring_fd = loader->ringBufferFd();
    if (ring_fd < 0)
    {
        return std::unexpected(EbpfError::kMapAccessFailed);
    }

    // Create ring buffer consumer with nullptr context initially
    // The context will be set via the callback closure
    ring_buffer* ring_buf = ring_buffer__new(ring_fd, ringBufferCallback, nullptr, nullptr);
    if (ring_buf == nullptr)
    {
        return std::unexpected(EbpfError::kMapAccessFailed);
    }

    return MigrationTracker{std::move(*loader), ring_buf, std::move(callback)};
}

auto MigrationTracker::start() -> std::expected<void, EbpfError>
{
    if (running_)
    {
        return {};  // Already running
    }

    auto result = loader_.attach();
    if (!result)
    {
        return std::unexpected(result.error());
    }

    running_ = true;
    return {};
}

void MigrationTracker::stop() noexcept
{
    if (!running_)
    {
        return;
    }

    loader_.detach();
    running_ = false;
}

auto MigrationTracker::poll(std::chrono::milliseconds timeout) -> int
{
    if (ring_buf_ == nullptr)
    {
        return -1;
    }

    int timeout_ms = static_cast<int>(timeout.count());
    return ring_buffer__poll(ring_buf_, timeout_ms);
}

auto MigrationTracker::setTargetPid(std::optional<std::uint32_t> pid)
    -> std::expected<void, EbpfError>
{
    std::uint32_t target = pid.value_or(0);
    return loader_.setTargetPid(target);
}

auto MigrationTracker::isRunning() const noexcept -> bool
{
    return running_;
}

auto MigrationTracker::eventCount() const noexcept -> std::uint64_t
{
    return event_count_.load(std::memory_order_relaxed);
}

auto MigrationTracker::ringBufferCallback(void* ctx, void* data, std::size_t size) -> int
{
    if (size < sizeof(migration_event))
    {
        return 0;  // Skip malformed event
    }

    auto* tracker = static_cast<MigrationTracker*>(ctx);
    if (tracker == nullptr || !tracker->callback_)
    {
        return 0;
    }

    // Convert raw BPF event to C++ MigrationEvent
    const auto* raw_event = static_cast<const migration_event*>(data);

    core::MigrationEvent event{};
    event.timestamp_ns = raw_event->timestamp_ns;
    event.pid = raw_event->pid;
    event.tid = raw_event->tid;
    event.src_cpu = raw_event->src_cpu;
    event.dst_cpu = raw_event->dst_cpu;

    // Copy command name from BPF event
    std::memcpy(event.comm.data(), raw_event->comm,
                std::min(sizeof(event.comm), sizeof(raw_event->comm)));

    tracker->callback_(event);
    tracker->event_count_.fetch_add(1, std::memory_order_relaxed);

    return 0;  // Continue processing
}

}  // namespace threveal::collection

/**
 *  @file       ebpf_loader.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  RAII wrapper for loading and managing eBPF programs.
 *
 *  Provides a safe C++ interface for the BPF program lifecycle using
 *  the auto-generated skeleton from libbpf.
 */

#ifndef THREVEAL_COLLECTION_EBPF_LOADER_HPP_
#define THREVEAL_COLLECTION_EBPF_LOADER_HPP_

#include "threveal/core/errors.hpp"

#include <cstdint>
#include <expected>
#include <string_view>

// Forward declaration of the generated skeleton structure (name from libbpf)
struct migration_tracker_bpf;

namespace threveal::collection
{

/**
 *  Error conditions that can occur during eBPF operations.
 */
enum class EbpfError : std::uint8_t
{
    /**
     *  Failed to open the BPF object.
     */
    kOpenFailed = 1,

    /**
     *  Failed to load the BPF program into the kernel.
     */
    kLoadFailed = 2,

    /**
     *  Failed to attach the BPF program to its hook point.
     */
    kAttachFailed = 3,

    /**
     *  The BPF program is not in a valid state for the operation.
     */
    kInvalidState = 4,

    /**
     *  Failed to access a BPF map.
     */
    kMapAccessFailed = 5,

    /**
     *  Permission denied (requires CAP_BPF or root).
     */
    kPermissionDenied = 6,
};

/**
 *  Converts an EbpfError to its human-readable string representation.
 *
 *  @param      error  The error to convert.
 *  @return     A string view describing the error condition.
 */
[[nodiscard]] constexpr auto toString(EbpfError error) noexcept -> std::string_view
{
    switch (error)
    {
        case EbpfError::kOpenFailed:
            return "failed to open BPF object";
        case EbpfError::kLoadFailed:
            return "failed to load BPF program";
        case EbpfError::kAttachFailed:
            return "failed to attach BPF program";
        case EbpfError::kInvalidState:
            return "BPF program in invalid state";
        case EbpfError::kMapAccessFailed:
            return "failed to access BPF map";
        case EbpfError::kPermissionDenied:
            return "permission denied for BPF operations";
    }
    return "unknown eBPF error";
}

/**
 *  RAII wrapper for the migration_tracker eBPF program.
 *
 *  EbpfLoader manages the complete lifecycle of the BPF program:
 *  open, load, attach, and destroy. It provides a safe C++ interface
 *  over the auto-generated libbpf skeleton.
 *
 *  This class is move-only; BPF resources cannot be safely copied.
 *
 *  Example usage:
 *  @code
 *      auto loader = EbpfLoader::create();
 *      if (!loader) {
 *          // Handle error
 *      }
 *      auto attach_result = loader->attach();
 *      if (!attach_result) {
 *          // Handle error
 *      }
 *      // BPF program is now active
 *      int ring_buffer_fd = loader->ringBufferFd();
 *  @endcode
 */
class EbpfLoader
{
  public:
    /**
     *  Creates and loads a new EbpfLoader instance.
     *
     *  Opens the BPF object and loads it into the kernel, but does not
     *  attach it yet. Call attach() to start tracing.
     *
     *  @return     An EbpfLoader on success, or EbpfError on failure.
     */
    [[nodiscard]] static auto create() -> std::expected<EbpfLoader, EbpfError>;

    /**
     *  Destroys the loader and releases all BPF resources.
     */
    ~EbpfLoader();

    // Move-only semantics
    EbpfLoader(EbpfLoader&& other) noexcept;
    auto operator=(EbpfLoader&& other) noexcept -> EbpfLoader&;
    EbpfLoader(const EbpfLoader&) = delete;
    auto operator=(const EbpfLoader&) -> EbpfLoader& = delete;

    /**
     *  Attaches the BPF program to its tracepoint.
     *
     *  After calling this, the program will start capturing migration events.
     *
     *  @return     Success or EbpfError on failure.
     */
    [[nodiscard]] auto attach() -> std::expected<void, EbpfError>;

    /**
     *  Detaches the BPF program from its tracepoint.
     *
     *  The program remains loaded but stops capturing events.
     */
    void detach() noexcept;

    /**
     *  Sets the target PID filter.
     *
     *  When set to a non-zero value, only migrations for the specified
     *  process (and its threads) will be captured.
     *
     *  @param      pid  Process ID to filter, or 0 to capture all.
     *  @return     Success or EbpfError on failure.
     */
    [[nodiscard]] auto setTargetPid(std::uint32_t pid) -> std::expected<void, EbpfError>;

    /**
     *  Returns the file descriptor for the events ring buffer.
     *
     *  This fd can be used with ring_buffer__new() to consume events.
     *
     *  @return     The ring buffer fd, or -1 if not valid.
     */
    [[nodiscard]] auto ringBufferFd() const noexcept -> int;

    /**
     *  Checks if the BPF program is currently attached.
     */
    [[nodiscard]] auto isAttached() const noexcept -> bool;

    /**
     *  Checks if the loader is in a valid state.
     */
    [[nodiscard]] auto isValid() const noexcept -> bool;

  private:
    explicit EbpfLoader(migration_tracker_bpf* skel) noexcept;

    migration_tracker_bpf* skel_;
    bool attached_{false};
};

}  // namespace threveal::collection

#endif  // THREVEAL_COLLECTION_EBPF_LOADER_HPP_

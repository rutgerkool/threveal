/**
 *  @file       ebpf_loader.hpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Wrapper for loading and managing eBPF programs.
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
 *  Wrapper for the migration_tracker eBPF program.
 */
class EbpfLoader
{
  public:
    /**
     *  Creates and loads a new EbpfLoader instance.
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
     *  @return     Success or EbpfError on failure.
     */
    [[nodiscard]] auto attach() -> std::expected<void, EbpfError>;

    /**
     *  Detaches the BPF program from its tracepoint.
     */
    void detach() noexcept;

    /**
     *  Sets the target PID filter.
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

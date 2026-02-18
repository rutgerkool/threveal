/**
 *  @file       ebpf_loader.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Implementation of the EbpfLoader class.
 */

#include "threveal/collection/ebpf_loader.hpp"

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <cerrno>
#include <cstdint>
#include <expected>
#include <utility>

// Suppress warnings from auto-generated skeleton code
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "migration_tracker.skel.h"
#pragma GCC diagnostic pop

namespace threveal::collection
{

namespace
{

auto errnoToEbpfError(int err) -> EbpfError
{
    // libbpf returns negative errno values
    int abs_err = (err < 0) ? -err : err;

    if (abs_err == EPERM || abs_err == EACCES)
    {
        return EbpfError::kPermissionDenied;
    }
    return EbpfError::kLoadFailed;
}

}  // namespace

void BpfSkeletonDeleter::operator()(migration_tracker_bpf* skel) const noexcept
{
    migration_tracker_bpf__destroy(skel);
}

EbpfLoader::EbpfLoader(BpfSkeletonPtr skel) noexcept : skel_(std::move(skel)) {}

EbpfLoader::~EbpfLoader()
{
    if (attached_)
    {
        detach();
    }
}

EbpfLoader::EbpfLoader(EbpfLoader&& other) noexcept
    : skel_(std::move(other.skel_)), attached_(std::exchange(other.attached_, false))
{
}

auto EbpfLoader::operator=(EbpfLoader&& other) noexcept -> EbpfLoader&
{
    if (this == &other)
    {
        return *this;
    }

    // Detach before destroying current skeleton
    if (attached_)
    {
        detach();
    }

    // Take ownership (old skel_ automatically destroyed by unique_ptr)
    skel_ = std::move(other.skel_);
    attached_ = std::exchange(other.attached_, false);

    return *this;
}

auto EbpfLoader::create() -> std::expected<EbpfLoader, EbpfError>
{
    // Configure options with explicit BTF path for older libbpf versions
    bpf_object_open_opts open_opts{};
    open_opts.sz = sizeof(open_opts);
    open_opts.btf_custom_path = "/sys/kernel/btf/vmlinux";

    // Open the BPF object with explicit BTF path
    BpfSkeletonPtr skel{migration_tracker_bpf__open_opts(&open_opts)};
    if (!skel)
    {
        if (errno == EPERM || errno == EACCES)
        {
            return std::unexpected(EbpfError::kPermissionDenied);
        }
        return std::unexpected(EbpfError::kOpenFailed);
    }

    // Load the BPF program into the kernel
    int err = migration_tracker_bpf__load(skel.get());
    if (err != 0)
    {
        return std::unexpected(errnoToEbpfError(err));
    }

    return EbpfLoader{std::move(skel)};
}

auto EbpfLoader::attach() -> std::expected<void, EbpfError>
{
    if (!skel_)
    {
        return std::unexpected(EbpfError::kInvalidState);
    }

    if (attached_)
    {
        return {};
    }

    int err = migration_tracker_bpf__attach(skel_.get());
    if (err != 0)
    {
        return std::unexpected(errnoToEbpfError(err));
    }

    attached_ = true;
    return {};
}

void EbpfLoader::detach() noexcept
{
    if (!skel_ || !attached_)
    {
        return;
    }

    migration_tracker_bpf__detach(skel_.get());
    attached_ = false;
}

auto EbpfLoader::setTargetPid(std::uint32_t pid) -> std::expected<void, EbpfError>
{
    if (!skel_)
    {
        return std::unexpected(EbpfError::kInvalidState);
    }

    int map_fd = bpf_map__fd(skel_->maps.migration_config);
    if (map_fd < 0)
    {
        return std::unexpected(EbpfError::kMapAccessFailed);
    }

    std::uint32_t key = 0;
    int err = bpf_map_update_elem(map_fd, &key, &pid, BPF_ANY);
    if (err != 0)
    {
        return std::unexpected(EbpfError::kMapAccessFailed);
    }

    return {};
}

auto EbpfLoader::ringBufferFd() const noexcept -> int
{
    if (!skel_)
    {
        return -1;
    }
    return bpf_map__fd(skel_->maps.events);
}

auto EbpfLoader::isAttached() const noexcept -> bool
{
    return skel_ && attached_;
}

auto EbpfLoader::isValid() const noexcept -> bool
{
    return skel_ != nullptr;
}

}  // namespace threveal::collection

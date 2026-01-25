/**
 *  @file       test_ebpf_loader.cpp
 *  @author     Rutger Kool <rutgerkool@gmail.com>
 *
 *  Unit tests for EbpfLoader.
 *
 *  Note: eBPF operations require CAP_BPF or root privileges.
 *  Tests that require privileges will be skipped if permissions are insufficient.
 */

#include "threveal/collection/ebpf_loader.hpp"

#include <catch2/catch_test_macros.hpp>
#include <unistd.h>
#include <utility>

using threveal::collection::EbpfError;
using threveal::collection::EbpfLoader;
using threveal::collection::toString;

namespace
{

auto hasEbpfPrivileges() -> bool
{
    return geteuid() == 0;
}

}  // namespace

TEST_CASE("EbpfError toString", "[collection][EbpfError]")
{
    REQUIRE(toString(EbpfError::kOpenFailed) == "failed to open BPF object");
    REQUIRE(toString(EbpfError::kLoadFailed) == "failed to load BPF program");
    REQUIRE(toString(EbpfError::kAttachFailed) == "failed to attach BPF program");
    REQUIRE(toString(EbpfError::kInvalidState) == "BPF program in invalid state");
    REQUIRE(toString(EbpfError::kMapAccessFailed) == "failed to access BPF map");
    REQUIRE(toString(EbpfError::kPermissionDenied) == "permission denied for BPF operations");
}

TEST_CASE("EbpfLoader creation requires privileges", "[collection][EbpfLoader]")
{
    auto loader = EbpfLoader::create();

    if (!hasEbpfPrivileges())
    {
        REQUIRE_FALSE(loader.has_value());
        REQUIRE((loader.error() == EbpfError::kPermissionDenied ||
                 loader.error() == EbpfError::kLoadFailed));
        return;
    }

    REQUIRE(loader.has_value());
    REQUIRE(loader->isValid());
    REQUIRE_FALSE(loader->isAttached());
}

TEST_CASE("EbpfLoader move semantics", "[collection][EbpfLoader]")
{
    if (!hasEbpfPrivileges())
    {
        SKIP("eBPF operations require root privileges");
    }

    auto loader1 = EbpfLoader::create();
    REQUIRE(loader1.has_value());
    REQUIRE(loader1->isValid());

    SECTION("move construction")
    {
        EbpfLoader loader2 = std::move(*loader1);
        REQUIRE(loader2.isValid());
        REQUIRE_FALSE(loader1->isValid());
    }

    SECTION("move assignment")
    {
        auto loader2 = EbpfLoader::create();
        REQUIRE(loader2.has_value());

        *loader2 = std::move(*loader1);
        REQUIRE(loader2->isValid());
        REQUIRE_FALSE(loader1->isValid());
    }
}

TEST_CASE("EbpfLoader attach and detach", "[collection][EbpfLoader]")
{
    if (!hasEbpfPrivileges())
    {
        SKIP("eBPF operations require root privileges");
    }

    auto loader = EbpfLoader::create();
    REQUIRE(loader.has_value());

    SECTION("attach succeeds")
    {
        auto result = loader->attach();
        REQUIRE(result.has_value());
        REQUIRE(loader->isAttached());
    }

    SECTION("detach after attach")
    {
        REQUIRE(loader->attach().has_value());
        loader->detach();
        REQUIRE_FALSE(loader->isAttached());
    }

    SECTION("double attach is idempotent")
    {
        REQUIRE(loader->attach().has_value());
        REQUIRE(loader->attach().has_value());
        REQUIRE(loader->isAttached());
    }
}

TEST_CASE("EbpfLoader ring buffer fd", "[collection][EbpfLoader]")
{
    if (!hasEbpfPrivileges())
    {
        SKIP("eBPF operations require root privileges");
    }

    auto loader = EbpfLoader::create();
    REQUIRE(loader.has_value());

    int fd = loader->ringBufferFd();
    REQUIRE(fd >= 0);
}

TEST_CASE("EbpfLoader setTargetPid", "[collection][EbpfLoader]")
{
    if (!hasEbpfPrivileges())
    {
        SKIP("eBPF operations require root privileges");
    }

    auto loader = EbpfLoader::create();
    REQUIRE(loader.has_value());

    SECTION("set valid PID")
    {
        REQUIRE(loader->setTargetPid(1234).has_value());
    }

    SECTION("set zero (capture all)")
    {
        REQUIRE(loader->setTargetPid(0).has_value());
    }
}

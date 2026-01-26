# Threveal Prerequisites

This document lists the system dependencies required to build and run Threveal.

## Build Dependencies

### Required (all builds)

- **CMake 3.25+**: Build system
- **GCC 13+ or Clang 17+**: C++23 compiler
- **vcpkg**: Package manager for C++ dependencies

### Required (eBPF support)

The following must be installed system-wide (not via vcpkg):
```bash
# Ubuntu 22.04+ base dependencies
sudo apt-get install -y libelf-dev zlib1g-dev clang \
    linux-tools-common linux-tools-$(uname -r)

# libbpf 1.3.0 from source (apt version is too old)
git clone https://github.com/libbpf/libbpf.git --depth 1 --branch v1.3.0
cd libbpf/src
make
sudo make install
echo "/usr/lib64" | sudo tee /etc/ld.so.conf.d/lib64.conf
sudo ldconfig
```

| Package | Purpose |
|---------|---------|
| `libelf-dev` | ELF parsing (libbpf build dependency) |
| `zlib1g-dev` | Compression (libbpf build dependency) |
| `clang` | Compiler with BPF target support |
| `linux-tools-*` | Provides `bpftool` for skeleton generation |
| `libbpf 1.3.0` | eBPF program loading (built from source) |

### Verification
```bash
# Verify libbpf
pkg-config --libs libbpf
# Expected: -lbpf

# Verify bpftool
bpftool version
# Expected: bpftool v7.x.x (or similar)

# Verify kernel BTF (required for CO-RE)
ls /sys/kernel/btf/vmlinux
# Must exist
```

## Runtime Requirements

### Permissions

Threveal requires elevated privileges for eBPF and PMU access:
```bash
# Option 1: Run as root
sudo ./threveal ...

# Option 2: Grant capabilities (recommended)
sudo setcap cap_perfmon,cap_bpf+ep ./threveal

# Option 3: Adjust system settings (less secure)
sudo sysctl kernel.perf_event_paranoid=1
```

### Kernel Requirements

| Feature | Minimum Kernel | Purpose |
|---------|----------------|---------|
| Hybrid PMU | 5.13+ | Separate cpu_core/cpu_atom PMU namespaces |
| eBPF CO-RE | 5.5+ | BTF-based BPF programs |
| Ring buffers | 5.8+ | Efficient eBPF-to-userspace data transfer |

**Recommended**: Linux 6.0+ for best hybrid CPU support.

### Hardware Requirements

- **CPU**: Intel 12th gen (Alder Lake) or newer hybrid architecture
- **BTF**: Kernel must be built with `CONFIG_DEBUG_INFO_BTF=y`

## CI/CD Notes

In GitHub Actions CI, libbpf 1.3.0 is built from source because Ubuntu's packaged version (0.5.0) is too old for modern kernel BTF formats.

eBPF tests that require `CAP_BPF` or root privileges will automatically skip in CI environments where these permissions are not available. The tests use Catch2's `SKIP()` macro to handle this gracefully.

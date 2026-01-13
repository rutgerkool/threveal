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
# Ubuntu 22.04+
sudo apt-get install libbpf-dev linux-tools-common linux-tools-$(uname -r) clang
```

| Package | Purpose |
|---------|---------|
| `libbpf-dev` | eBPF program loading library |
| `linux-tools-*` | Provides `bpftool` for skeleton generation |
| `clang` | Compiler with BPF target support |

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

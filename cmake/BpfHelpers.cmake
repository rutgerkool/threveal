# BpfHelpers.cmake
#
# Helper functions for building eBPF programs with libbpf and CO-RE.
#
# Provides:
#   bpf_generate_vmlinux_h() - Generate vmlinux.h from kernel BTF
#   bpf_compile_program()    - Compile a .bpf.c file to .bpf.o
#   bpf_generate_skeleton()  - Generate skeleton header from .bpf.o

include(CMakeParseArguments)

# Find required tools
find_program(CLANG_EXECUTABLE clang REQUIRED)
find_program(BPFTOOL_EXECUTABLE bpftool REQUIRED)

# Verify clang supports BPF target
execute_process(
    COMMAND ${CLANG_EXECUTABLE} -print-targets
    OUTPUT_VARIABLE CLANG_TARGETS
    ERROR_QUIET
)
if(NOT CLANG_TARGETS MATCHES "bpf")
    message(FATAL_ERROR "Clang does not support BPF target. Please install a version with BPF support.")
endif()

message(STATUS "Found clang: ${CLANG_EXECUTABLE}")
message(STATUS "Found bpftool: ${BPFTOOL_EXECUTABLE}")

#
# bpf_generate_vmlinux_h(<output_file>)
#
# Generate vmlinux.h from the running kernel's BTF information.
# This file contains all kernel type definitions needed for CO-RE eBPF programs.
#
# Arguments:
#   output_file - Path where vmlinux.h will be generated
#
function(bpf_generate_vmlinux_h output_file)
    set(BTF_FILE "/sys/kernel/btf/vmlinux")

    if(NOT EXISTS ${BTF_FILE})
        message(FATAL_ERROR
            "Kernel BTF not found at ${BTF_FILE}. "
            "Ensure your kernel was built with CONFIG_DEBUG_INFO_BTF=y"
        )
    endif()

    # Get the directory of the output file
    get_filename_component(output_dir ${output_file} DIRECTORY)

    # Generate vmlinux.h at configure time
    message(STATUS "Generating vmlinux.h from kernel BTF...")
    execute_process(
        COMMAND ${BPFTOOL_EXECUTABLE} btf dump file ${BTF_FILE} format c
        OUTPUT_FILE ${output_file}
        RESULT_VARIABLE result
        ERROR_VARIABLE error_output
    )

    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Failed to generate vmlinux.h: ${error_output}")
    endif()

    message(STATUS "Generated vmlinux.h at ${output_file}")
endfunction()

#
# bpf_compile_program(<target_name>
#     SOURCE <source_file>
#     OUTPUT <output_file>
#     [INCLUDE_DIRS <dir1> <dir2> ...]
# )
#
# Compile an eBPF program from .bpf.c to .bpf.o using clang.
#
# Arguments:
#   target_name  - Name of the CMake custom target to create
#   SOURCE       - Path to the .bpf.c source file
#   OUTPUT       - Path where .bpf.o will be written
#   INCLUDE_DIRS - Additional include directories (optional)
#
function(bpf_compile_program target_name)
    cmake_parse_arguments(BPF "" "SOURCE;OUTPUT" "INCLUDE_DIRS" ${ARGN})

    if(NOT BPF_SOURCE)
        message(FATAL_ERROR "bpf_compile_program: SOURCE is required")
    endif()
    if(NOT BPF_OUTPUT)
        message(FATAL_ERROR "bpf_compile_program: OUTPUT is required")
    endif()

    # Build include flags
    set(include_flags "")
    foreach(dir ${BPF_INCLUDE_DIRS})
        list(APPEND include_flags "-I${dir}")
    endforeach()

    # Compile BPF program
    # Flags explanation:
    #   -g              : Generate debug info (required for CO-RE relocations)
    #   -O2             : Optimization level (required for some BPF verifier checks)
    #   -target bpf     : Compile for BPF target
    #   -D__TARGET_ARCH_x86 : Define target architecture for vmlinux.h
    #   -c              : Compile only, don't link
    add_custom_command(
        OUTPUT ${BPF_OUTPUT}
        COMMAND ${CLANG_EXECUTABLE}
            -g -O2
            -target bpf
            -D__TARGET_ARCH_x86
            ${include_flags}
            -c ${BPF_SOURCE}
            -o ${BPF_OUTPUT}
        DEPENDS ${BPF_SOURCE}
        COMMENT "Compiling BPF program ${BPF_SOURCE}"
        VERBATIM
    )

    add_custom_target(${target_name} DEPENDS ${BPF_OUTPUT})
endfunction()

#
# bpf_generate_skeleton(<target_name>
#     INPUT <bpf_object_file>
#     OUTPUT <skeleton_header>
#     [DEPENDS <target>]
# )
#
# Generate a BPF skeleton header from a compiled .bpf.o file.
# The skeleton provides type-safe C API for loading and interacting with the BPF program.
#
# Arguments:
#   target_name - Name of the CMake custom target to create
#   INPUT       - Path to the .bpf.o file
#   OUTPUT      - Path where the skeleton .skel.h will be written
#   DEPENDS     - Target that builds the .bpf.o file (optional)
#
function(bpf_generate_skeleton target_name)
    cmake_parse_arguments(BPF "" "INPUT;OUTPUT;DEPENDS" "" ${ARGN})

    if(NOT BPF_INPUT)
        message(FATAL_ERROR "bpf_generate_skeleton: INPUT is required")
    endif()
    if(NOT BPF_OUTPUT)
        message(FATAL_ERROR "bpf_generate_skeleton: OUTPUT is required")
    endif()

    # Generate skeleton header
    add_custom_command(
        OUTPUT ${BPF_OUTPUT}
        COMMAND ${BPFTOOL_EXECUTABLE} gen skeleton ${BPF_INPUT} > ${BPF_OUTPUT}
        DEPENDS ${BPF_INPUT}
        COMMENT "Generating BPF skeleton ${BPF_OUTPUT}"
        VERBATIM
    )

    if(BPF_DEPENDS)
        add_custom_target(${target_name} DEPENDS ${BPF_OUTPUT})
        add_dependencies(${target_name} ${BPF_DEPENDS})
    else()
        add_custom_target(${target_name} DEPENDS ${BPF_OUTPUT})
    endif()
endfunction()

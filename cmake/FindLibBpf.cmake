# FindLibBpf.cmake
#
# Find the system libbpf library.
#
# This module defines:
#   LIBBPF_FOUND        - True if libbpf was found
#   LIBBPF_INCLUDE_DIRS - Include directories for libbpf
#   LIBBPF_LIBRARIES    - Libraries to link against
#   LibBpf::LibBpf      - Imported target for libbpf
#
# The following variables can be set as hints:
#   LIBBPF_ROOT_DIR     - Root directory to search for libbpf

include(FindPackageHandleStandardArgs)

# Try pkg-config first (preferred method)
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(PC_LIBBPF QUIET libbpf)
endif()

# Find the include directory
find_path(LIBBPF_INCLUDE_DIR
    NAMES bpf/libbpf.h
    HINTS
        ${PC_LIBBPF_INCLUDEDIR}
        ${PC_LIBBPF_INCLUDE_DIRS}
        ${LIBBPF_ROOT_DIR}/include
    PATHS
        /usr/include
        /usr/local/include
)

# Find the library
find_library(LIBBPF_LIBRARY
    NAMES bpf
    HINTS
        ${PC_LIBBPF_LIBDIR}
        ${PC_LIBBPF_LIBRARY_DIRS}
        ${LIBBPF_ROOT_DIR}/lib
    PATHS
        /usr/lib
        /usr/lib/x86_64-linux-gnu
        /usr/local/lib
)

# Handle the REQUIRED/QUIET arguments and set LIBBPF_FOUND
find_package_handle_standard_args(LibBpf
    REQUIRED_VARS LIBBPF_LIBRARY LIBBPF_INCLUDE_DIR
    VERSION_VAR PC_LIBBPF_VERSION
)

# Set output variables
if(LIBBPF_FOUND)
    set(LIBBPF_LIBRARIES ${LIBBPF_LIBRARY})
    set(LIBBPF_INCLUDE_DIRS ${LIBBPF_INCLUDE_DIR})

    # Create imported target if it doesn't exist
    if(NOT TARGET LibBpf::LibBpf)
        add_library(LibBpf::LibBpf UNKNOWN IMPORTED)
        set_target_properties(LibBpf::LibBpf PROPERTIES
            IMPORTED_LOCATION "${LIBBPF_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LIBBPF_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(LIBBPF_INCLUDE_DIR LIBBPF_LIBRARY)

# FindLibBpf.cmake
#
# Find the system libbpf library.
#
# This module defines:
#   LIBBPF_FOUND        - True if libbpf was found
#   LIBBPF_INCLUDE_DIRS - Include directories for libbpf
#   LIBBPF_LIBRARIES    - Libraries to link against
#   LibBpf::LibBpf      - Imported target for libbpf

include(FindPackageHandleStandardArgs)

# Find the library first (prefer /usr/lib64 for manually installed newer versions)
# This determines which version we'll use before checking pkg-config
find_library(LIBBPF_LIBRARY
    NAMES bpf
    PATHS
        /usr/lib64
        /usr/local/lib64
        /usr/local/lib
    NO_DEFAULT_PATH
)

# Fallback to system paths if not found in preferred locations
if(NOT LIBBPF_LIBRARY)
    find_library(LIBBPF_LIBRARY NAMES bpf)
endif()

# Find the include directory based on where we found the library
if(LIBBPF_LIBRARY MATCHES "^/usr/lib64")
    # Use headers from manually installed version
    find_path(LIBBPF_INCLUDE_DIR
        NAMES bpf/libbpf.h
        PATHS /usr/local/include /usr/include
        NO_DEFAULT_PATH
    )
    set(LIBBPF_VERSION "1.3.0")
else()
    # Use system headers
    find_path(LIBBPF_INCLUDE_DIR
        NAMES bpf/libbpf.h
        PATHS /usr/include
    )
    # Try to get version from pkg-config
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(PC_LIBBPF QUIET libbpf)
        if(PC_LIBBPF_VERSION)
            set(LIBBPF_VERSION ${PC_LIBBPF_VERSION})
        else()
            set(LIBBPF_VERSION "unknown")
        endif()
    else()
        set(LIBBPF_VERSION "unknown")
    endif()
endif()

# Handle the REQUIRED/QUIET arguments and set LIBBPF_FOUND
find_package_handle_standard_args(LibBpf
    REQUIRED_VARS LIBBPF_LIBRARY LIBBPF_INCLUDE_DIR
    VERSION_VAR LIBBPF_VERSION
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

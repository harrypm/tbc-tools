cmake_minimum_required(VERSION 3.16)

# Enforce out-of-source builds before project() to catch issues early
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Please create a build directory and run:
    mkdir build
    cd build
    cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..")
endif()

# Also prevent building directly in subdirectories of the source tree
# Exception: Allow 'build' and 'build-*' directories as they are gitignored
get_filename_component(REAL_SOURCE_DIR "${CMAKE_SOURCE_DIR}" REALPATH)
get_filename_component(REAL_BINARY_DIR "${CMAKE_BINARY_DIR}" REALPATH)
get_filename_component(BUILD_DIR_NAME "${CMAKE_BINARY_DIR}" NAME)
file(RELATIVE_PATH REL_PATH "${REAL_SOURCE_DIR}" "${REAL_BINARY_DIR}")

# Check if build directory is inside source tree (rel path doesn't start with ..)
# but allow 'build' or 'build-*' directory names
if(NOT REL_PATH MATCHES "^\\.\\." AND 
   NOT BUILD_DIR_NAME STREQUAL "build" AND 
   NOT BUILD_DIR_NAME MATCHES "^build-")
    message(FATAL_ERROR "Building within the source tree (except 'build/' or 'build-*/') is not allowed.
    Build directory: ${REAL_BINARY_DIR}
    Source directory: ${REAL_SOURCE_DIR}
    
    Please use one of these approaches:
    
    1. Standard build directory (recommended):
       mkdir build
       cd build
       cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
    
    2. Named build directory:
       mkdir build-release
       cd build-release
       cmake -DCMAKE_BUILD_TYPE=Release ..
    
    3. Out-of-tree build:
       mkdir ../ld-decode-tools-build
       cd ../ld-decode-tools-build
       cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ../ld-decode-tools")
endif()

project(ld-decode-tools)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake_modules")
include(CTest)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set output directories for binaries and libraries
# All executables will be placed in build/bin
# All libraries will be placed in build/lib
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# For multi-configuration generators (like MSVC)
foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_BINARY_DIR}/lib)
endforeach()

# Options that can be specified with -D

# Needed for ezpwd as it uses alternative operators
if(MSVC)
    add_compile_options(/permissive-)
endif()

option(BUILD_PYTHON
    "Build and install ld-decode's Python library and tools"
    OFF
)

# Path to ezpwd headers (can be overridden by external builds)
set(EZPWD_DIR "" CACHE PATH
    "Path to ezpwd C++ headers (directory containing ezpwd/rs_base)"
)

if(NOT EZPWD_DIR AND DEFINED ENV{EZPWD_DIR})
    set(EZPWD_DIR "$ENV{EZPWD_DIR}" CACHE PATH
        "Path to ezpwd C++ headers (directory containing ezpwd/rs_base)"
        FORCE
    )
endif()

if(NOT EZPWD_DIR)
    set(_EZPWD_CANDIDATES
        "${CMAKE_SOURCE_DIR}/src/efm-decoder/libs/ezpwd/c++"
        "${CMAKE_SOURCE_DIR}/src/efm-decoder/libs/ezpwd"
    )
    foreach(_EZPWD_CANDIDATE IN LISTS _EZPWD_CANDIDATES)
        if(EXISTS "${_EZPWD_CANDIDATE}/ezpwd/rs_base")
            set(EZPWD_DIR "${_EZPWD_CANDIDATE}" CACHE PATH
                "Path to ezpwd C++ headers (directory containing ezpwd/rs_base)"
                FORCE
            )
            break()
        endif()
    endforeach()
    unset(_EZPWD_CANDIDATES)
    unset(_EZPWD_CANDIDATE)
endif()

if(NOT EZPWD_DIR OR NOT EXISTS "${EZPWD_DIR}/ezpwd/rs_base")
    message(FATAL_ERROR "EZPWD_DIR is not set or invalid. Set -DEZPWD_DIR=/path/to/ezpwd/c++ or initialize the ezpwd submodule at src/efm-decoder/libs/ezpwd. (Nix builds pass this automatically).")
endif()

# Check for dependencies

# When using Qt 6.3, you can replace the code block below with qt_standard_project_setup()
set(CMAKE_AUTOMOC ON)
include(GNUInstallDirs)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Sql Svg)
message(STATUS "Qt Version: ${QT_VERSION}")

find_package(PkgConfig REQUIRED)

pkg_check_modules(FFTW REQUIRED IMPORTED_TARGET fftw3)
set(FFTW_INCLUDE_DIR ${FFTW_INCLUDE_DIRS})
set(FFTW_LIBRARY PkgConfig::FFTW)

# Build metadata (set by Nix or other build systems)
set(APP_BRANCH "nix" CACHE STRING "Build source identifier")
set(APP_COMMIT "0.0.0" CACHE STRING "Build version identifier")

add_compile_definitions(APP_BRANCH=\"${APP_BRANCH}\")
add_compile_definitions(APP_COMMIT=\"${APP_COMMIT}\")

# Subdirectories

add_subdirectory(src/ld-analyse)
add_subdirectory(src/ld-chroma-decoder)
add_subdirectory(src/ld-chroma-decoder/encoder)
add_subdirectory(src/ld-disc-stacker)
add_subdirectory(src/ld-discmap)
add_subdirectory(src/ld-dropout-correct)
add_subdirectory(src/ld-export-decode-metadata)
add_subdirectory(src/ld-export-metadata)
add_subdirectory(src/metadata-converter)
add_subdirectory(src/ld-lds-converter)
add_subdirectory(src/ld-process-vbi)
add_subdirectory(src/ld-process-vits)
add_subdirectory(src/library)
add_subdirectory(src/efm-decoder)

if(BUILD_TESTING)
    add_subdirectory(src/library/filter/testfilter)
    add_subdirectory(src/library/tbc/testlinenumber)
    add_subdirectory(src/library/tbc/testmetadata)
    add_subdirectory(src/library/tbc/testvbidecoder)
    add_subdirectory(src/library/tbc/testvitcdecoder)

    # Tests for the ld-decode tools.
    # Most of the tests expect that you have cloned (or symlinked) the
    # ld-decode-testdata repo within the source directory as "testdata".
    # Chroma tests run sequentially to avoid file conflicts

    set(SCRIPTS_DIR ${CMAKE_SOURCE_DIR}/scripts)
    set(TESTDATA_DIR ${CMAKE_SOURCE_DIR}/testdata)

    add_test(
        NAME chroma-ntsc-rgb
        COMMAND ${SCRIPTS_DIR}/test-chroma
            --build ${CMAKE_BINARY_DIR}
            --system ntsc
            --expect-psnr 25
            --expect-psnr-range 0.5
    )

    add_test(
        NAME chroma-ntsc-ycbcr
        COMMAND ${SCRIPTS_DIR}/test-chroma
            --build ${CMAKE_BINARY_DIR}
            --system ntsc
            --expect-psnr 25
            --expect-psnr-range 0.5
            --input-format yuv
    )
    set_tests_properties(chroma-ntsc-ycbcr PROPERTIES DEPENDS chroma-ntsc-rgb)

    add_test(
        NAME chroma-pal-rgb
        COMMAND ${SCRIPTS_DIR}/test-chroma
            --build ${CMAKE_BINARY_DIR}
            --system pal
            --expect-psnr 25
            --expect-psnr-range 0.5
    )
    set_tests_properties(chroma-pal-rgb PROPERTIES DEPENDS chroma-ntsc-ycbcr)

    add_test(
        NAME chroma-pal-ycbcr
        COMMAND ${SCRIPTS_DIR}/test-chroma
            --build ${CMAKE_BINARY_DIR}
            --system pal
            --expect-psnr 25
            --expect-psnr-range 0.5
            --input-format yuv
    )
    set_tests_properties(chroma-pal-ycbcr PROPERTIES DEPENDS chroma-pal-rgb)

    # Tests using pre-generated TBC files (ld-decode/ld-cut not part of this repo)
    # Note: These tests were previously named ld-cut-ntsc and decode-ntsc-cav but both
    # used the same source file. Now using pre-generated TBC, they test the same pipeline.
    add_test(
        NAME decode-pretbc-ntsc-cav
        COMMAND ${SCRIPTS_DIR}/test-decode-pretbc
            --build ${CMAKE_BINARY_DIR}
            --decoder mono --decoder ntsc2d --decoder ntsc3d
            --expect-frames 29
            --expect-bpsnr 43.3
            --expect-vbi 9151563,15925840,15925840
            --expect-efm-samples 40572
            ${CMAKE_SOURCE_DIR}/test-data/ntsc/ve-snw-cut
    )

    add_test(
        NAME decode-pretbc-ntsc-clv
        COMMAND ${SCRIPTS_DIR}/test-decode-pretbc
            --build ${CMAKE_BINARY_DIR}
            --no-efm-timecodes
            --expect-frames 4
            --expect-bpsnr 37.6
            --expect-vbi 9167913,15785241,15785241
            ${CMAKE_SOURCE_DIR}/test-data/ntsc/issue176
    )

    add_test(
        NAME decode-pretbc-pal-cav
        COMMAND ${SCRIPTS_DIR}/test-decode-pretbc
            --build ${CMAKE_BINARY_DIR}
            --pal
            --decoder mono --decoder pal2d --decoder transform2d --decoder transform3d
            --expect-frames 4
            --expect-bpsnr 38.4
            --expect-vbi 9151527,16065688,16065688
            --expect-vitc 2,10,8,13,4,3,0,1
            --expect-efm-samples 5292
            ${CMAKE_SOURCE_DIR}/test-data/pal/jason-testpattern
    )

    add_test(
        NAME decode-pretbc-pal-clv
        COMMAND ${SCRIPTS_DIR}/test-decode-pretbc
            --build ${CMAKE_BINARY_DIR}
            --pal
            --no-efm
            --expect-frames 9
            --expect-bpsnr 30.3
            --expect-vbi 0,8449774,8449774
            ${CMAKE_SOURCE_DIR}/test-data/pal/kagemusha-leadout-cbar
    )

    add_test(
        NAME decode-pretbc-pal-ggv
        COMMAND ${SCRIPTS_DIR}/test-decode-pretbc
            --build ${CMAKE_BINARY_DIR}
            --pal
            --no-efm  # GGV discs do not contain EFM data
            --expect-frames 24
            --expect-vbi 9152512,15730528,15730528
            ${CMAKE_SOURCE_DIR}/test-data/pal/ggv-mb-1khz
    )
endif()

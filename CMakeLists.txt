cmake_minimum_required(VERSION 3.16)
if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()

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
       mkdir ../tbc-tools-build
       cd ../tbc-tools-build
       cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ../tbc-tools")
endif()

project(tbc-tools)

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

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Sql Svg Network)
message(STATUS "Qt Version: ${QT_VERSION}")

find_package(PkgConfig REQUIRED)

pkg_check_modules(FFTW REQUIRED IMPORTED_TARGET fftw3)
set(FFTW_INCLUDE_DIR ${FFTW_INCLUDE_DIRS})
set(FFTW_LIBRARY PkgConfig::FFTW)

find_package(onnxruntime CONFIG QUIET)
if(NOT TARGET onnxruntime::onnxruntime)
    set(_ONNXRUNTIME_SEARCH_ROOTS
        ${ONNXRUNTIME_ROOT}
        $ENV{ONNXRUNTIME_ROOT}
    )
    list(FILTER _ONNXRUNTIME_SEARCH_ROOTS EXCLUDE REGEX "^$")
    list(REMOVE_DUPLICATES _ONNXRUNTIME_SEARCH_ROOTS)
    unset(ONNXRUNTIME_INCLUDE_DIR CACHE)
    unset(ONNXRUNTIME_LIBRARY CACHE)
    if(WIN32)
        unset(ONNXRUNTIME_DLL CACHE)
    endif()
    find_path(ONNXRUNTIME_INCLUDE_DIR
        NAMES onnxruntime_cxx_api.h
        PATHS
            ${_ONNXRUNTIME_SEARCH_ROOTS}
        PATH_SUFFIXES
            include
        NO_DEFAULT_PATH
    )
    find_library(ONNXRUNTIME_LIBRARY
        NAMES onnxruntime
        PATHS
            ${_ONNXRUNTIME_SEARCH_ROOTS}
        PATH_SUFFIXES
            lib
        NO_DEFAULT_PATH
    )
    if(WIN32)
        find_file(ONNXRUNTIME_DLL
            NAMES onnxruntime.dll
            PATHS
                ${_ONNXRUNTIME_SEARCH_ROOTS}
            PATH_SUFFIXES
                bin
                lib
            NO_DEFAULT_PATH
        )
    endif()

    if(NOT ONNXRUNTIME_INCLUDE_DIR OR NOT ONNXRUNTIME_LIBRARY OR (WIN32 AND NOT ONNXRUNTIME_DLL))
        message(FATAL_ERROR "Could not find ONNX Runtime. Set ONNXRUNTIME_ROOT to an SDK containing include/ and lib/, or provide onnxruntimeConfig.cmake.")
    endif()

    add_library(onnxruntime::onnxruntime SHARED IMPORTED GLOBAL)
    if(WIN32)
        set_target_properties(onnxruntime::onnxruntime PROPERTIES
            IMPORTED_IMPLIB "${ONNXRUNTIME_LIBRARY}"
            IMPORTED_IMPLIB_RELEASE "${ONNXRUNTIME_LIBRARY}"
            IMPORTED_LOCATION "${ONNXRUNTIME_DLL}"
            IMPORTED_LOCATION_RELEASE "${ONNXRUNTIME_DLL}"
            INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}"
        )
    else()
        set_target_properties(onnxruntime::onnxruntime PROPERTIES
            IMPORTED_LOCATION "${ONNXRUNTIME_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}"
        )
    endif()
    unset(_ONNXRUNTIME_SEARCH_ROOTS)
    unset(ONNXRUNTIME_DLL)
endif()

function(stage_windows_onnxruntime_runtime target_name)
    if(NOT WIN32)
        return()
    endif()
    if(NOT TARGET onnxruntime::onnxruntime)
        return()
    endif()

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:onnxruntime::onnxruntime>"
            "$<TARGET_FILE_DIR:${target_name}>"
        VERBATIM
    )
endfunction()

function(stage_windows_qt_platform_runtime target_name)
    if(NOT WIN32)
        return()
    endif()
    if(NOT TARGET Qt::Core)
        return()
    endif()

    set(_qt_plugins_root "")
    set(_qt_runtime_bin_dir "")

    if(DEFINED Qt6_DIR AND EXISTS "${Qt6_DIR}")
        get_filename_component(_qt6_share_dir "${Qt6_DIR}" DIRECTORY)
        get_filename_component(_qt6_prefix_dir "${_qt6_share_dir}" DIRECTORY)
        set(_qt6_plugins_candidate "${_qt6_prefix_dir}/Qt6/plugins")
        if(EXISTS "${_qt6_plugins_candidate}/platforms/qwindows.dll")
            set(_qt_plugins_root "${_qt6_plugins_candidate}")
            set(_qt_runtime_bin_dir "${_qt6_prefix_dir}/bin")
        endif()
    endif()

    if(NOT _qt_plugins_root)
        get_target_property(_qt_core_imported_release Qt::Core IMPORTED_LOCATION_RELEASE)
        if(_qt_core_imported_release)
            get_filename_component(_qt_core_bin_dir "${_qt_core_imported_release}" DIRECTORY)
            get_filename_component(_qt_core_prefix_dir "${_qt_core_bin_dir}" DIRECTORY)
            set(_qt_core_plugins_candidate "${_qt_core_prefix_dir}/plugins")
            if(EXISTS "${_qt_core_plugins_candidate}/platforms/qwindows.dll")
                set(_qt_plugins_root "${_qt_core_plugins_candidate}")
                set(_qt_runtime_bin_dir "${_qt_core_bin_dir}")
            endif()
        endif()
    endif()

    if(NOT _qt_plugins_root)
        get_target_property(_qt_core_imported_default Qt::Core IMPORTED_LOCATION)
        if(_qt_core_imported_default)
            get_filename_component(_qt_core_bin_dir "${_qt_core_imported_default}" DIRECTORY)
            get_filename_component(_qt_core_prefix_dir "${_qt_core_bin_dir}" DIRECTORY)
            set(_qt_core_plugins_candidate "${_qt_core_prefix_dir}/plugins")
            if(EXISTS "${_qt_core_plugins_candidate}/platforms/qwindows.dll")
                set(_qt_plugins_root "${_qt_core_plugins_candidate}")
                set(_qt_runtime_bin_dir "${_qt_core_bin_dir}")
            endif()
        endif()
    endif()

    if(NOT _qt_plugins_root)
        message(FATAL_ERROR "Could not locate Qt plugin runtime root (with platforms/qwindows.dll) for target ${target_name}.")
    endif()

    set(_qt_conf_local "${CMAKE_BINARY_DIR}/qt.conf")
    file(WRITE "${_qt_conf_local}" "[Paths]\nPrefix = .\nPlugins = .\n")

    set(_qt_optional_plugin_dirs
        generic
        iconengines
        imageformats
        networkinformation
        sqldrivers
        styles
        tls
    )
    set(_qt_plugin_copy_commands)
    foreach(_qt_plugin_dir IN LISTS _qt_optional_plugin_dirs)
        if(EXISTS "${_qt_plugins_root}/${_qt_plugin_dir}")
            list(APPEND _qt_plugin_copy_commands
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${_qt_plugins_root}/${_qt_plugin_dir}"
                    "$<TARGET_FILE_DIR:${target_name}>/${_qt_plugin_dir}"
            )
        endif()
    endforeach()

    set(_qt_optional_runtime_dlls
        libcrypto-3-x64.dll
        libssl-3-x64.dll
    )
    set(_qt_runtime_copy_commands)
    if(_qt_runtime_bin_dir)
        foreach(_qt_runtime_dll IN LISTS _qt_optional_runtime_dlls)
            if(EXISTS "${_qt_runtime_bin_dir}/${_qt_runtime_dll}")
                list(APPEND _qt_runtime_copy_commands
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_qt_runtime_bin_dir}/${_qt_runtime_dll}"
                        "$<TARGET_FILE_DIR:${target_name}>"
                )
            endif()
        endforeach()
    endif()

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${target_name}>/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_qt_plugins_root}/platforms/qwindows.dll"
            "$<TARGET_FILE_DIR:${target_name}>/platforms"
        ${_qt_plugin_copy_commands}
        ${_qt_runtime_copy_commands}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_qt_conf_local}"
            "$<TARGET_FILE_DIR:${target_name}>/qt.conf"
        VERBATIM
    )
endfunction()

# Build metadata (set by Nix or other build systems)
set(APP_BRANCH "nix" CACHE STRING "Build source identifier")
set(APP_COMMIT "0.0.0" CACHE STRING "Build version identifier")
if(NOT APP_VERSION)
    set(_APP_VERSION_DEFAULT "0.0.0")
    if(EXISTS "${CMAKE_SOURCE_DIR}/vcpkg.json")
        file(READ "${CMAKE_SOURCE_DIR}/vcpkg.json" _VCPKG_JSON_CONTENT)
        string(REGEX MATCH "\"version\"[ \t\r\n]*:[ \t\r\n]*\"([^\"]+)\"" _VCPKG_VERSION_MATCH "${_VCPKG_JSON_CONTENT}")
        if(CMAKE_MATCH_1)
            set(_APP_VERSION_DEFAULT "${CMAKE_MATCH_1}")
        endif()
        unset(_VCPKG_JSON_CONTENT)
        unset(_VCPKG_VERSION_MATCH)
    endif()
    set(APP_VERSION "${_APP_VERSION_DEFAULT}")
    unset(_APP_VERSION_DEFAULT)
endif()
set(APP_VERSION "${APP_VERSION}" CACHE STRING "Release version identifier")

add_compile_definitions(APP_BRANCH=\"${APP_BRANCH}\")
add_compile_definitions(APP_COMMIT=\"${APP_COMMIT}\")
add_compile_definitions(APP_VERSION=\"${APP_VERSION}\")

# Subdirectories

add_subdirectory(src/ld-analyse)
add_subdirectory(src/ld-chroma-decoder)
add_subdirectory(src/ld-chroma-decoder/encoder)
add_subdirectory(src/ld-disc-stacker)
add_subdirectory(src/ld-discmap)
add_subdirectory(src/ld-dropout-correct)
add_subdirectory(src/tbc-export-metadata)
add_subdirectory(src/tbc-metadata-converter)
add_subdirectory(src/audio-align)
add_subdirectory(src/ld-lds-converter)
add_subdirectory(src/ld-process-vbi)
add_subdirectory(src/ld-process-vits)
add_subdirectory(src/ac3-decoder)
add_subdirectory(src/library)
add_subdirectory(src/efm-decoder)
add_subdirectory(src/tbc-video-export)

if(BUILD_TESTING)
    add_subdirectory(src/library/filter/testfilter)
    add_subdirectory(src/library/tbc/testlinenumber)
    add_subdirectory(src/library/tbc/testmetadata)
    add_subdirectory(src/library/tbc/testvbidecoder)
    add_subdirectory(src/library/tbc/testvitcdecoder)
    add_subdirectory(src/ld-chroma-decoder/testsourcefield)
    add_subdirectory(src/ld-analyse/testexportarguments)

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

    # Targeted support/regression checks:
    # decode using a freshly generated SQLite metadata DB from current fixtures.
    add_test(
        NAME decode-pretbc-freshdb-ntsc-clv
        COMMAND ${SCRIPTS_DIR}/test-decode-pretbc
            --build ${CMAKE_BINARY_DIR}
            --fresh-db-decode
            --no-efm-timecodes
            --expect-frames 4
            --expect-bpsnr 37.6
            --expect-vbi 9167913,15785241,15785241
            ${CMAKE_SOURCE_DIR}/test-data/ntsc/issue176
            ${CMAKE_BINARY_DIR}/testout/decode-freshdb-ntsc-clv
    )
    set_tests_properties(decode-pretbc-freshdb-ntsc-clv PROPERTIES
        LABELS "decode-targeted;decode-regression"
    )

    add_test(
        NAME decode-pretbc-freshdb-pal-clv
        COMMAND ${SCRIPTS_DIR}/test-decode-pretbc
            --build ${CMAKE_BINARY_DIR}
            --fresh-db-decode
            --pal
            --no-efm
            --expect-frames 9
            --expect-bpsnr 30.3
            --expect-vbi 0,8449774,8449774
            ${CMAKE_SOURCE_DIR}/test-data/pal/kagemusha-leadout-cbar
            ${CMAKE_BINARY_DIR}/testout/decode-freshdb-pal-clv
    )
    set_tests_properties(decode-pretbc-freshdb-pal-clv PROPERTIES
        LABELS "decode-targeted;decode-regression"
    )
endif()

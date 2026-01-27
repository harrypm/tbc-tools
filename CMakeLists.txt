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

option(BUILD_LDF_READER
    "build ld_ldf_reader"
    ON
)

option(BUILD_PYTHON
    "Build and install ld-decode's Python library and tools"
    OFF
)

# Check for dependencies

# When using Qt 6.3, you can replace the code block below with qt_standard_project_setup()
set(CMAKE_AUTOMOC ON)
include(GNUInstallDirs)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Sql)
message(STATUS "Qt Version: ${QT_VERSION}")

find_package(PkgConfig REQUIRED)

if(NOT MSVC)
    pkg_check_modules(FFTW IMPORTED_TARGET fftw3)
    if(FFTW_FOUND)
        # .....
        set(FFTW_INCLUDE_DIR ${FFTW_INCLUDE_DIRS})
        set(FFTW_LIBRARY PkgConfig::FFTW)
    else()
        find_package(FFTW REQUIRED)
    endif()
else()
    # pkg-config seems to
    # result in trying to link to m.lib
    # which breaks build on
    # MSVC, so just use the cmake.config from
    # the vcpkg install instead.
    find_package(FFTW3 REQUIRED)
    set(FFTW_INCLUDE_DIR ${FFTW3_INCLUDE_DIRS})
    find_library(FFTW_LIBRARY ${FFTW3_LIBRARIES} ${FFTW3_LIBRARY_DIRS})
endif()

# Get the Git branch and revision

execute_process(
    COMMAND git rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND git log -1 --format=%h
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Check if there are uncommitted changes
execute_process(
    COMMAND git diff-index --quiet HEAD --
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RESULT_VARIABLE GIT_DIRTY
)

# Add -dirty suffix if there are uncommitted changes
if(GIT_DIRTY)
    set(GIT_COMMIT_HASH "${GIT_COMMIT_HASH}-dirty")
endif()

add_compile_definitions(APP_BRANCH=\"${GIT_BRANCH}\")
add_compile_definitions(APP_COMMIT=\"${GIT_COMMIT_HASH}\")

# Subdirectories

add_subdirectory(src/ld-analyse)
add_subdirectory(src/ld-chroma-decoder)
add_subdirectory(src/ld-chroma-decoder/encoder)
add_subdirectory(src/ld-disc-stacker)
add_subdirectory(src/ld-discmap)
add_subdirectory(src/ld-dropout-correct)
add_subdirectory(src/ld-export-decode-metadata)
add_subdirectory(src/ld-export-metadata)
add_subdirectory(src/ld-json-converter)
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
    include(LdDecodeTests)
endif()

if(BUILD_LDF_READER)
    add_subdirectory(src/ld-ldf-reader)
endif()

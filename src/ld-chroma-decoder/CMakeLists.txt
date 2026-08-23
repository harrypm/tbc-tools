# ==========================================
# 1. Enable CUDA support and architecture configuration
# ==========================================
set(_ldchroma_enable_cuda_default ON)
if(APPLE)
    set(_ldchroma_enable_cuda_default OFF)
endif()
option(LDCHROMA_ENABLE_CUDA "Enable CUDA acceleration for ld-chroma-decoder when available" ${_ldchroma_enable_cuda_default})
unset(_ldchroma_enable_cuda_default)

if(APPLE AND LDCHROMA_ENABLE_CUDA)
    message(STATUS "CUDA acceleration is currently unsupported on macOS; forcing LDCHROMA_ENABLE_CUDA=OFF")
    set(LDCHROMA_ENABLE_CUDA OFF CACHE BOOL "Enable CUDA acceleration for ld-chroma-decoder when available" FORCE)
endif()
set(LDCHROMA_HAS_CUDA OFF)
if(LDCHROMA_ENABLE_CUDA)
    include(CheckLanguage)
    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        set(LDCHROMA_HAS_CUDA ON)
        set(CMAKE_CUDA_RUNTIME_LIBRARY Shared)

        # NVCC 11.8 needs an older host compiler for compilation, but CMake can propagate that
        # host compiler's runtime search paths into non-CUDA executable links. This causes
        # libstdc++/libgcc symbol ABI mismatches when the rest of the project uses a newer C++ toolchain.
        # Keep CUDA-specific paths while stripping GCC 11 runtime directories from implicit CUDA link paths.
        function(_sanitize_cuda_implicit_link_dirs input_var output_var)
            set(_sanitized)
            foreach(_entry IN LISTS ${input_var})
                if(_entry STREQUAL "")
                    continue()
                endif()

                # Some NVCC/CMake detections return malformed combined entries like
                # ".../lib64-L.../nvvm/lib". Split these into individual paths.
                string(REPLACE "-L" ";" _expanded_entries "${_entry}")
                foreach(_expanded_entry IN LISTS _expanded_entries)
                    if(_expanded_entry STREQUAL "")
                        continue()
                    endif()
                    if(_expanded_entry MATCHES "([/-])gcc-11(\\.|-)" OR
                       _expanded_entry MATCHES "([/-])gcc-wrapper-11(\\.|-)" OR
                       _expanded_entry MATCHES "/lib/gcc/.*/11\\.")
                        continue()
                    endif()
                    if(NOT _expanded_entry IN_LIST _sanitized)
                        list(APPEND _sanitized "${_expanded_entry}")
                    endif()
                endforeach()
            endforeach()
            set(${output_var} "${_sanitized}" PARENT_SCOPE)
        endfunction()

        _sanitize_cuda_implicit_link_dirs(CMAKE_CUDA_HOST_IMPLICIT_LINK_DIRECTORIES _cuda_host_link_dirs_sanitized)
        if(_cuda_host_link_dirs_sanitized)
            set(CMAKE_CUDA_HOST_IMPLICIT_LINK_DIRECTORIES "${_cuda_host_link_dirs_sanitized}")
        endif()

        _sanitize_cuda_implicit_link_dirs(CMAKE_CUDA_IMPLICIT_LINK_DIRECTORIES _cuda_link_dirs_sanitized)
        if(_cuda_link_dirs_sanitized)
            set(CMAKE_CUDA_IMPLICIT_LINK_DIRECTORIES "${_cuda_link_dirs_sanitized}")
        endif()

        unset(_cuda_host_link_dirs_sanitized)
        unset(_cuda_link_dirs_sanitized)

        # Set minimum GPU architecture to Pascal (61 = GTX 1000 series), while including newer architectures
        set(CMAKE_CUDA_ARCHITECTURES 61 75 86 89)
        set(CMAKE_CUDA_STANDARD 17)
        set(CMAKE_CUDA_STANDARD_REQUIRED ON)
        find_package(CUDAToolkit REQUIRED)
    else()
        message(STATUS "CUDA compiler not found; building ld-chroma-decoder without CUDA kernel acceleration")
    endif()
else()
    message(STATUS "LDCHROMA_ENABLE_CUDA=OFF; building ld-chroma-decoder without CUDA kernel acceleration")
endif()

if(LDCHROMA_HAS_CUDA)
    add_compile_definitions(LDCHROMA_HAS_CUDA=1)
else()
    add_compile_definitions(LDCHROMA_HAS_CUDA=0)
endif()

# ==========================================
# 2. Locate external deep-learning and GPU dependencies
# ==========================================

find_path(ONNXRUNTIME_INCLUDE_DIR NAMES onnxruntime_cxx_api.h PATHS
    ${ONNXRUNTIME_ROOT}/include
    ${ONNXRUNTIME_ROOT}/include/onnxruntime
    $ENV{ONNXRUNTIME_ROOT}/include
    $ENV{ONNXRUNTIME_ROOT}/include/onnxruntime
)
find_library(ONNXRUNTIME_LIBRARY NAMES onnxruntime PATHS ${ONNXRUNTIME_ROOT}/lib $ENV{ONNXRUNTIME_ROOT}/lib)

if(NOT ONNXRUNTIME_INCLUDE_DIR OR NOT ONNXRUNTIME_LIBRARY)
    message(FATAL_ERROR "Could not find ONNX Runtime. Please set ONNXRUNTIME_ROOT.")
endif()

# ==========================================
# 3. Core build logic (integrating GPU filtering)
# ==========================================
# For M_PI constant
add_compile_definitions(_USE_MATH_DEFINES)

# Embed nnTransform3D model data
set(_nn_model_file "${CMAKE_CURRENT_SOURCE_DIR}/resources/chroma_net_v2.onnx")
if(NOT EXISTS "${_nn_model_file}")
    message(FATAL_ERROR "Missing nnTransform3D model: ${_nn_model_file}")
endif()
file(READ "${_nn_model_file}" _nn_model_hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _nn_model_array "${_nn_model_hex}")
string(LENGTH "${_nn_model_hex}" _nn_model_hex_len)
math(EXPR _nn_model_size "${_nn_model_hex_len} / 2")
set(_nn_model_header "${CMAKE_CURRENT_BINARY_DIR}/chroma_net_v2_onnx_data.h")
set(_nn_model_source "${CMAKE_CURRENT_BINARY_DIR}/chroma_net_v2_onnx_data.cpp")
file(WRITE "${_nn_model_header}"
    "#pragma once\n"
    "#include <cstddef>\n"
    "extern const unsigned char kChromaNetV2OnnxData[];\n"
    "extern const std::size_t kChromaNetV2OnnxDataSize;\n")
file(WRITE "${_nn_model_source}"
    "#include \"chroma_net_v2_onnx_data.h\"\n"
    "const unsigned char kChromaNetV2OnnxData[] = {${_nn_model_array}};\n"
    "const std::size_t kChromaNetV2OnnxDataSize = ${_nn_model_size};\n")

set(lddecode_chroma_sources
    decoder.cpp
    decoderpool.cpp
    comb.cpp
    componentframe.cpp
    framecanvas.cpp
    outputwriter.cpp
    palcolour.cpp
    secamdecoder.cpp
    sourcefield.cpp
    transformpal.cpp
    transformpal2d.cpp
    transformpal3d.cpp
    monodecoder.cpp
)
if(LDCHROMA_HAS_CUDA)
    list(APPEND lddecode_chroma_sources nnTransform3D_kernel.cu)
endif()

add_library(tbc-chroma STATIC
    ${lddecode_chroma_sources}
    ${_nn_model_source}
)
if(LDCHROMA_HAS_CUDA)
    target_link_libraries(tbc-chroma PRIVATE
        CUDA::cufft
        CUDA::cudart
    )
endif()

target_include_directories(tbc-chroma PUBLIC
    .
    ${CMAKE_CURRENT_BINARY_DIR}
    ${ONNXRUNTIME_INCLUDE_DIR}
)
target_link_libraries(tbc-chroma PRIVATE
    Qt::Core
    ${FFTW_LIBRARY}
    onnxruntime::onnxruntime
    tbc-library
)

target_include_directories(tbc-chroma PRIVATE ${FFTW_INCLUDE_DIR})

# ==========================================
# 4. Executable target (ld-chroma-decoder)
# ==========================================
add_executable(ld-chroma-decoder
    decoder.cpp
    decoderpool.cpp
    main.cpp
    monodecoder.cpp
    ntscdecoder.cpp
    paldecoder.cpp
    secamdecoder.cpp
)

set_target_properties(tbc-chroma PROPERTIES AUTOMOC ON)

target_include_directories(ld-chroma-decoder PRIVATE ${FFTW_INCLUDE_DIR})
target_link_libraries(ld-chroma-decoder PRIVATE
    Qt::Core
    ${FFTW_LIBRARY}
    onnxruntime::onnxruntime
    tbc-library
    tbc-chroma
)
stage_windows_onnxruntime_runtime(ld-chroma-decoder)
if(LDCHROMA_HAS_CUDA)
    target_link_libraries(ld-chroma-decoder PRIVATE
        CUDA::cufft
        CUDA::cudart
    )
endif()

install(TARGETS ld-chroma-decoder)
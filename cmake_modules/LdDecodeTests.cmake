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

# End of LdDecodeTests.cmake
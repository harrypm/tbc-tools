# Unit test for the composite (measurement) vectorscope acquisition engine
# (composite_vectorscope.cpp).  Synthetic in-memory PAL/NTSC frames with a
# known burst angle, amplitude and V-switch state; deterministic, no file
# I/O.
add_executable(testcompositevectorscope
    testcompositevectorscope.cpp
    ../composite_vectorscope.cpp
    ../tbc_cvbs_conversion.cpp
)

target_include_directories(testcompositevectorscope PRIVATE
    ..
)

target_link_libraries(testcompositevectorscope PRIVATE
    Qt::Core
    tbc-library
)

add_test(NAME testcompositevectorscope COMMAND testcompositevectorscope)

add_executable(testsourcefield
    testsourcefield.cpp
    ../sourcefield.cpp
)

target_include_directories(testsourcefield PRIVATE
    ..
)

target_link_libraries(testsourcefield PRIVATE
    Qt::Core
    tbc-library
)

add_test(NAME testsourcefield COMMAND testsourcefield)

add_executable(testfilter
    testfilter.cpp
)

target_link_libraries(testfilter PRIVATE Qt::Core tbc-library)

add_test(NAME testfilter COMMAND testfilter)

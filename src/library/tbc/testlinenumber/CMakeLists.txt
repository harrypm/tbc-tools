add_executable(testlinenumber
    testlinenumber.cpp
)

target_link_libraries(testlinenumber PRIVATE Qt::Core tbc-library)

add_test(NAME testlinenumber COMMAND testlinenumber)

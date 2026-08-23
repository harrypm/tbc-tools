add_executable(testmetadata
    testmetadata.cpp
)

target_link_libraries(testmetadata PRIVATE Qt::Core tbc-library)

add_test(NAME testmetadata COMMAND testmetadata)

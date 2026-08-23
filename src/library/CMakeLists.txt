add_library(tbc-library STATIC
    tbc/dropouts.cpp
    tbc/filters.cpp
    tbc/jsonio.cpp
    tbc/sqliteio.cpp
    tbc/tbcmetadata.cpp
    tbc/logging.cpp
    tbc/navigation.cpp
    tbc/sourceaudio.cpp
    tbc/sourcevideo.cpp
    tbc/vbidecoder.cpp
    tbc/videoiddecoder.cpp
    tbc/vitcdecoder.cpp
)

target_include_directories(tbc-library PUBLIC filter tbc)
target_include_directories(tbc-library PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tbc-library PRIVATE Qt::Core Qt::Sql)

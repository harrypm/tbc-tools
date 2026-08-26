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
    tbc/vbi/nabts_page.cpp
    tbc/vbi/naplps_font.cpp
    tbc/vbi/teletext_page.cpp
    tbc/vbi/vbi_coding.cpp
    tbc/vbi/nabts_packet.cpp
    tbc/vbi/nabts_data_group.cpp
    tbc/vbi/nabts_record.cpp
    tbc/vbi/naplps_pdi.cpp
    tbc/vbi/naplps_code_env.cpp
    tbc/vbi/naplps_state.cpp
    tbc/vbi/naplps_interpreter.cpp
    tbc/vbi/naplps_raster.cpp
    tbc/vbi/teletext_decoder.cpp
)
if(WIN32)
    target_compile_definitions(tbc-library PRIVATE NOMINMAX)
endif()

target_include_directories(tbc-library PUBLIC filter tbc tbc/vbi)
target_include_directories(tbc-library PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tbc-library PRIVATE Qt::Core Qt::Sql)

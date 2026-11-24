set(TBC_VIDEO_EXPORT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(TBC_VIDEO_EXPORT_BUILD_SHARE_DIR "${CMAKE_BINARY_DIR}/share/tbc-video-export")
set(TBC_VIDEO_EXPORT_BUILD_BIN_DIR "${CMAKE_BINARY_DIR}/bin")
set(TBC_VIDEO_EXPORT_BUILD_BIN "${TBC_VIDEO_EXPORT_BUILD_BIN_DIR}/tbc-video-export")
set(TBC_VIDEO_EXPORT_WRAPPER_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/tbc-video-export.in")

file(MAKE_DIRECTORY "${TBC_VIDEO_EXPORT_BUILD_SHARE_DIR}")
file(MAKE_DIRECTORY "${TBC_VIDEO_EXPORT_BUILD_BIN_DIR}")
# Refresh the build-tree python copy on every build (configure-time file(COPY)
# goes stale after source edits, so the build tree would keep running old
# python via build/bin/tbc-video-export).
add_custom_target(tbc-video-export-share-copy ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${TBC_VIDEO_EXPORT_SOURCE_DIR}"
        "${TBC_VIDEO_EXPORT_BUILD_SHARE_DIR}/src"
    COMMENT "Refreshing tbc-video-export python sources in build tree")
configure_file("${TBC_VIDEO_EXPORT_WRAPPER_TEMPLATE}"
               "${TBC_VIDEO_EXPORT_BUILD_BIN}"
               @ONLY)
file(CHMOD "${TBC_VIDEO_EXPORT_BUILD_BIN}"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
install(PROGRAMS "${TBC_VIDEO_EXPORT_BUILD_BIN}"
        DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY "${TBC_VIDEO_EXPORT_SOURCE_DIR}/tbc_video_export"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/tbc-video-export/src")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/tbc-video-export")

if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME video-export-runtime
            COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_SOURCE_DIR}/scripts/test-video-export-runtime.py"
                --build "${CMAKE_BINARY_DIR}"
                --source "${CMAKE_SOURCE_DIR}"
        )
        set_tests_properties(video-export-runtime PROPERTIES SKIP_RETURN_CODE 77)

        # End-to-end export suites (scripts/test-export-e2e.py): GUI option
        # contract, tbc-video-export CLI matrix, and the tbc-analyse Export tab
        # driven headlessly through every profile/option it offers. Outputs are
        # verified with ffprobe; JUnit reports land in the build dir.
        foreach(_e2e_suite contract cli gui)
            add_test(
                NAME export-e2e-${_e2e_suite}
                COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_SOURCE_DIR}/scripts/test-export-e2e.py"
                    --source "${CMAKE_SOURCE_DIR}"
                    --build "${CMAKE_BINARY_DIR}"
                    --suite ${_e2e_suite}
                    --level full
                    --junit "${CMAKE_BINARY_DIR}/export-e2e-${_e2e_suite}.xml"
            )
            set_tests_properties(export-e2e-${_e2e_suite} PROPERTIES
                SKIP_RETURN_CODE 77
                TIMEOUT 3600
                LABELS "e2e;export")
        endforeach()
    endif()
endif()

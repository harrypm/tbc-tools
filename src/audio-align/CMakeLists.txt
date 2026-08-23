add_executable(tbc-audio-align
    audioalignmentdialog.cpp
    audioalignmentdialog.ui
    audioalignmentutil.cpp
    main.cpp
)

target_link_libraries(tbc-audio-align PRIVATE Qt::Core Qt::Gui Qt::Widgets tbc-library)

target_compile_definitions(tbc-audio-align PRIVATE
    AUDIO_ALIGN_VENDOR_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vendor/vhs_decode_auto_audio_align"
)

set(_audio_align_vendor_dir "${CMAKE_CURRENT_SOURCE_DIR}/vendor/vhs_decode_auto_audio_align")
add_custom_command(TARGET tbc-audio-align POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:tbc-audio-align>/vendor/vhs_decode_auto_audio_align"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_audio_align_vendor_dir}/VhsDecodeAutoAudioAlign.exe"
        "${_audio_align_vendor_dir}/Binah.dll"
        "${_audio_align_vendor_dir}/LICENSE-app.txt"
        "${_audio_align_vendor_dir}/LICENSE-Binah.txt"
        "${_audio_align_vendor_dir}/CHANGELOG.md"
        "${_audio_align_vendor_dir}/build-info.txt"
        "${_audio_align_vendor_dir}/VENDOR_INFO.txt"
        "$<TARGET_FILE_DIR:tbc-audio-align>/vendor/vhs_decode_auto_audio_align"
)

install(TARGETS tbc-audio-align)
install(DIRECTORY "${_audio_align_vendor_dir}/" DESTINATION ${CMAKE_INSTALL_BINDIR}/vendor/vhs_decode_auto_audio_align)

if(BUILD_TESTING)
    add_executable(test-audio-align-runtime
        testaudioalignmentruntime.cpp
        audioalignmentutil.cpp
    )
    target_link_libraries(test-audio-align-runtime PRIVATE Qt::Core tbc-library)
    target_compile_definitions(test-audio-align-runtime PRIVATE
        AUDIO_ALIGN_VENDOR_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vendor/vhs_decode_auto_audio_align"
    )

    add_test(
        NAME aaa-runtime
        COMMAND $<TARGET_FILE:test-audio-align-runtime>
            --json "${CMAKE_SOURCE_DIR}/src/tbc-video-export/tests/files/pal_svideo.tbc.json"
            --input-audio "${CMAKE_SOURCE_DIR}/src/tbc-video-export/tests/files/audio_48_16.wav"
            --output-audio "${CMAKE_BINARY_DIR}/test-artifacts/aaa-runtime/aligned.flac"
            --rf-video-sample-rate-hz 40000000
    )
    set_tests_properties(aaa-runtime PROPERTIES SKIP_RETURN_CODE 77)
endif()

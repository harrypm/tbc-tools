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

    # Detection test: verifies the AAA resolver (the same one ld-analyse uses)
    # actually finds the vendored VhsDecodeAutoAudioAlign executable via the
    # application-dir-relative vendor lookup + AUDIO_ALIGN_VENDOR_DIR. Fails if
    # not detected; skips (77) if detected but not launchable in this tree
    # (e.g. the build-tree .exe with no Mono on PATH — launchability of the
    # self-contained AppImage is validated by the Linux bundle verifier).
    add_test(
        NAME aaa-detection
        COMMAND $<TARGET_FILE:test-audio-align-runtime> --detect-aaa
    )
    set_tests_properties(aaa-detection PROPERTIES SKIP_RETURN_CODE 77)

    # Auto-detect selection test: verifies the linear/hifi detectors pick the
    # right tracks for the standard vhs-decode naming and never target the RF
    # <stem>-video.flac dump. Pure logic (temp fixtures) — no ffmpeg/AAA.
    add_test(
        NAME aaa-detect-inputs
        COMMAND $<TARGET_FILE:test-audio-align-runtime> --detect-inputs
    )

    # RF source-rate provision test: verifies detectRfSourceSampleRateFromJson
    # returns an explicit videoParameters.rfSourceSampleRate and returns 0 (no
    # fallback to the decoded videoParameters.sampleRate) when absent. Enforces
    # the boundary rule that the RF timebase is user-provided / explicit-only.
    add_test(
        NAME aaa-detect-rf-source-rate
        COMMAND $<TARGET_FILE:test-audio-align-runtime> --detect-rf-source-rate
    )
endif()

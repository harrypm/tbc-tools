# For M_PI constant
add_compile_definitions(_USE_MATH_DEFINES)

set(ld-analyse_SOURCES
    blacksnranalysisdialog.cpp blacksnranalysisdialog.ui
    busydialog.cpp busydialog.ui
    ../audio-align/audioalignmentdialog.cpp ../audio-align/audioalignmentdialog.h ../audio-align/audioalignmentdialog.ui
    ../audio-align/audioalignmentutil.cpp ../audio-align/audioalignmentutil.h
    closedcaptionsdialog.cpp closedcaptionsdialog.ui
    efmhandlerdialog.cpp efmhandlerdialog.h
    exportdialog.cpp exportdialog.ui
    exportarguments.cpp exportarguments.h
    metadataconversiondialog.cpp metadataconversiondialog.ui
    metadatastatusdialog.cpp metadatastatusdialog.ui
    notesviewerdialog.cpp notesviewerdialog.h
    teletextviewerdialog.cpp teletextviewerdialog.h
    main.cpp
    mainwindow.cpp mainwindow.ui
    timelinemarkerslider.cpp timelinemarkerslider.h
    metadataconverterutil.cpp
    ../tbc-export-metadata/metadataexportdialog.cpp ../tbc-export-metadata/metadataexportdialog.h ../tbc-export-metadata/metadataexportdialog.ui
    oscilloscopedialog.cpp oscilloscopedialog.ui
    rgbscopedialog.cpp rgbscopedialog.h
    yuvrangedialog.cpp yuvrangedialog.h
    vectorscopedialog.cpp vectorscopedialog.ui
    waveformmonitordialog.cpp waveformmonitordialog.h
    waveformmonitorwidget.cpp waveformmonitorwidget.h
    tbc_cvbs_conversion.cpp tbc_cvbs_conversion.h
    amplitude_conversion.h
    cvbs_signal_constants.h
    fieldtimingdialog.cpp fieldtimingdialog.h
    fieldtimingwidget.cpp fieldtimingwidget.h
    aboutdialog.cpp aboutdialog.ui
    updatechecker.cpp updatechecker.h
    cudapluginmanager.cpp cudapluginmanager.h
    pluginmanagerdialog.cpp pluginmanagerdialog.h
    videoparametersdialog.cpp videoparametersdialog.ui
    chromadecoderconfigdialog.cpp chromadecoderconfigdialog.ui
    tbcsource.cpp
    vbidialog.cpp vbidialog.ui
    configuration.cpp
    dropoutanalysisdialog.cpp dropoutanalysisdialog.ui
    visibledropoutanalysisdialog.cpp visibledropoutanalysisdialog.ui
    whitesnranalysisdialog.cpp whitesnranalysisdialog.ui
    plotwidget.cpp
)

qt_add_resources(ld-analyse_SOURCES ld-analyse-resources.qrc)

if(WIN32)
set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/appicon.rc")

add_executable(ld-analyse MACOSX_BUNDLE
    ${ld-analyse_SOURCES}
	${APP_ICON_RESOURCE_WINDOWS})
elseif(UNIX)
	add_executable(ld-analyse MACOSX_BUNDLE
    ${ld-analyse_SOURCES})
endif()

if(APPLE)
    set(APP_ICON_RESOURCE_MACOS "${CMAKE_CURRENT_SOURCE_DIR}/Graphics/appicon.icns")
    target_sources(ld-analyse PRIVATE ${APP_ICON_RESOURCE_MACOS})
    set_source_files_properties(${APP_ICON_RESOURCE_MACOS} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    set_target_properties(ld-analyse PROPERTIES MACOSX_BUNDLE_ICON_FILE "appicon.icns")
endif()


target_include_directories(ld-analyse PRIVATE
    ${FFTW_INCLUDE_DIR}
)

target_link_libraries(ld-analyse PRIVATE
    Qt::Core Qt::Gui Qt::Widgets Qt::Svg Qt::Network
    lddecode-library lddecode-chroma
)
add_executable(tbc-efm-handler
    efmhandler-main.cpp
    efmhandlerdialog.cpp
    efmhandlerdialog.h
)

target_link_libraries(tbc-efm-handler PRIVATE
    Qt::Core Qt::Gui Qt::Widgets
    lddecode-library
)

set(_audio_align_vendor_dir "${CMAKE_SOURCE_DIR}/src/audio-align/vendor/vhs_decode_auto_audio_align")
target_compile_definitions(ld-analyse PRIVATE
    AUDIO_ALIGN_VENDOR_DIR="${_audio_align_vendor_dir}"
)
add_custom_command(TARGET ld-analyse POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ld-analyse>/vendor/vhs_decode_auto_audio_align"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_audio_align_vendor_dir}/VhsDecodeAutoAudioAlign.exe"
        "${_audio_align_vendor_dir}/Binah.dll"
        "${_audio_align_vendor_dir}/LICENSE-app.txt"
        "${_audio_align_vendor_dir}/LICENSE-Binah.txt"
        "${_audio_align_vendor_dir}/CHANGELOG.md"
        "${_audio_align_vendor_dir}/build-info.txt"
        "${_audio_align_vendor_dir}/VENDOR_INFO.txt"
        "$<TARGET_FILE_DIR:ld-analyse>/vendor/vhs_decode_auto_audio_align"
)

if(WIN32)
    target_compile_definitions(ld-analyse PRIVATE
        NOMINMAX
        QUSB_LIBRARY
    )
    set_property(TARGET ld-analyse PROPERTY WIN32_EXECUTABLE true)
endif()

if(UNIX)
	install(TARGETS ld-analyse BUNDLE DESTINATION bin)
	install(TARGETS tbc-efm-handler)
	
	# Install desktop integration files directly to XDG standard locations
	# This avoids hanging issues with xdg-* commands run via sudo
	
	# Install .tbc mime type definition
	install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/install/application-video-tbc.xml
		DESTINATION share/mime/packages)
	
	# Install application icons at various sizes
	install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/16-analyse.png
		DESTINATION share/icons/hicolor/16x16/apps
		RENAME ld-analyse.png)
	install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/32-analyse.png
		DESTINATION share/icons/hicolor/32x32/apps
		RENAME ld-analyse.png)
	install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/64-analyse.png
		DESTINATION share/icons/hicolor/64x64/apps
		RENAME ld-analyse.png)
	install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/Graphics/128-analyse.png
		DESTINATION share/icons/hicolor/128x128/apps
		RENAME ld-analyse.png)
	
	# Install desktop menu entry
	install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/install/ld-analyse.desktop
		DESTINATION share/applications)
	
	# Post-install: update caches (only if update-* commands are available)
	# These are safe to fail and won't hang
	install(CODE "execute_process(COMMAND which update-mime-database OUTPUT_QUIET ERROR_QUIET RESULT_VARIABLE _have_update_mime)")
	install(CODE "if(NOT _have_update_mime)
		execute_process(COMMAND update-mime-database \${CMAKE_INSTALL_PREFIX}/share/mime OUTPUT_QUIET ERROR_QUIET)
	endif()")
	install(CODE "execute_process(COMMAND which update-desktop-database OUTPUT_QUIET ERROR_QUIET RESULT_VARIABLE _have_update_desktop)")
	install(CODE "if(NOT _have_update_desktop)
		execute_process(COMMAND update-desktop-database \${CMAKE_INSTALL_PREFIX}/share/applications OUTPUT_QUIET ERROR_QUIET)
	endif()")
	install(CODE "execute_process(COMMAND which gtk-update-icon-cache OUTPUT_QUIET ERROR_QUIET RESULT_VARIABLE _have_gtk_icon)")
	install(CODE "if(NOT _have_gtk_icon)
		execute_process(COMMAND gtk-update-icon-cache -f -t \${CMAKE_INSTALL_PREFIX}/share/icons/hicolor OUTPUT_QUIET ERROR_QUIET)
	endif()")
else()
	install(
			TARGETS ld-analyse
			BUNDLE DESTINATION bin
	)
	install(TARGETS tbc-efm-handler)
endif()

add_executable(tbc-export-metadata
    audacity.cpp
    closedcaptions.cpp
    csv.cpp
    exportdropouts.cpp
    exportmetadata.cpp
    ffmetadata.cpp
    jsonio.cpp
    metadataconverter.cpp
    vitcffmetadata.cpp
    metadataexportdialog.cpp
    metadataexportdialog.ui
    main.cpp
)
target_link_libraries(tbc-export-metadata PRIVATE Qt::Core Qt::Gui Qt::Widgets tbc-library)

install(TARGETS tbc-export-metadata)

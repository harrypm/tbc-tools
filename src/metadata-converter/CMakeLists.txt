add_executable(tbc-metadata-converter
    dropouts.cpp
    jsonio.cpp
    lddecodemetadata.cpp
    jsonconverter.cpp
    metadataconversiondialog.cpp
    metadataconversiondialog.ui
    metadataconverterutil.cpp
    main.cpp
)

target_link_libraries(tbc-metadata-converter PRIVATE Qt::Core Qt::Gui Qt::Widgets Qt::Sql lddecode-library)

install(TARGETS tbc-metadata-converter)

add_executable(tbc-metadata-converter
    jsonconverter.cpp
    metadataconversiondialog.cpp
    metadataconversiondialog.ui
    metadataconverterutil.cpp
    main.cpp
)

target_link_libraries(tbc-metadata-converter PRIVATE Qt::Core Qt::Gui Qt::Widgets Qt::Sql tbc-library)

install(TARGETS tbc-metadata-converter)

if(BUILD_TESTING)
    add_executable(testmetadataconverterutil
        testmetadataconverterutil.cpp
        metadataconverterutil.cpp
    )
    target_link_libraries(testmetadataconverterutil PRIVATE Qt::Core)
    add_test(NAME testmetadataconverterutil COMMAND testmetadataconverterutil)
endif()

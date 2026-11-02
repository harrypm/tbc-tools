# Headless round-trip test for the Themes-menu theme persistence keys in
# Configuration (theme.keepChoice + theme.mode). Uses Qt's test-mode config
# sandbox; no display required.
add_executable(testthemeconfiguration
    testthemeconfiguration.cpp
    ../configuration.cpp
)

target_include_directories(testthemeconfiguration PRIVATE
    ..
)

target_link_libraries(testthemeconfiguration PRIVATE
    Qt::Core Qt::Gui Qt::Widgets
    tbc-library
)

add_test(NAME testthemeconfiguration COMMAND testthemeconfiguration)

# Headless regression test for the chroma decoder configuration dialog load
# sequence (user decoder selection preserved across resetGui/updateGuiLoaded).
add_executable(testchromaconfigdialog
    testchromaconfigdialog.cpp
    ../chromadecoderconfigdialog.cpp
    ../chromadecoderconfigdialog.ui
    ../tbcsource.cpp
)

target_include_directories(testchromaconfigdialog PRIVATE
    ..
)

target_link_libraries(testchromaconfigdialog PRIVATE
    Qt::Core Qt::Gui Qt::Widgets
    tbc-library tbc-chroma
)

add_test(NAME testchromaconfigdialog COMMAND testchromaconfigdialog)
# Headless: no display required (CI runners, Nix sandbox builds).
set_tests_properties(testchromaconfigdialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

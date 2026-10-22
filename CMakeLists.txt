cmake_minimum_required(VERSION 3.21)

project(live-logic-control
    VERSION 1.0.1
    DESCRIPTION "Linux editor for the Blackstar Live Logic MIDI Controller"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)

add_executable(live-logic-control
    src/main.cpp
    src/profile.cpp
    src/profile.h
    src/protocol.cpp
    src/protocol.h
    src/midi.cpp
    src/midi.h
    src/widgets.cpp
    src/widgets.h
    src/devicewidget.cpp
    src/devicewidget.h
    src/mainwindow.cpp
    src/mainwindow.h
    resources.qrc
)

target_link_libraries(live-logic-control PRIVATE
    Qt6::Widgets
    PkgConfig::ALSA
)

target_compile_options(live-logic-control PRIVATE
    -Wall -Wextra -Wpedantic
)

target_compile_definitions(live-logic-control PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
)

install(TARGETS live-logic-control RUNTIME DESTINATION bin)
install(FILES packaging/live-logic-control.desktop
    DESTINATION share/applications
)
install(FILES assets/live-logic-control.svg
    DESTINATION share/icons/hicolor/scalable/apps
    RENAME live-logic-control.svg
)
install(FILES assets/live-logic-control.png
    DESTINATION share/icons/hicolor/256x256/apps
    RENAME live-logic-control.png
)

include(CTest)
if(BUILD_TESTING)
    add_executable(profile-tests
        tests/test_profile.cpp
        src/profile.cpp
        src/protocol.cpp
    )
    target_link_libraries(profile-tests PRIVATE Qt6::Core)
    target_include_directories(profile-tests PRIVATE src)
    target_compile_options(profile-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME profile-tests COMMAND profile-tests)
endif()

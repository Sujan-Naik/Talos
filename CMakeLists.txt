cmake_minimum_required(VERSION 3.20)
project(Talos)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

qt_standard_project_setup()


add_executable(Talos src/main.cpp
        include/window/MainWindow.h
        src/window/MainWindow.cpp
        src/window/MainWindow.cpp
        include/ChatWidget.h
        src/ChatWidget.cpp
        include/CaptureOverlay.h)

find_package(PkgConfig REQUIRED)

pkg_check_modules(TESSERACT REQUIRED tesseract)
pkg_check_modules(LEPTONICA REQUIRED lept)

target_include_directories(Talos PRIVATE ${TESSERACT_INCLUDE_DIRS} ${LEPTONICA_INCLUDE_DIRS})

target_link_libraries(Talos PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        ${TESSERACT_LIBRARIES}
        ${LEPTONICA_LIBRARIES}

)

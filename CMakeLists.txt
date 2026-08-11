cmake_minimum_required(VERSION 3.20)
project(Talos)

include(FetchContent)

FetchContent_Declare(
        whisper
        GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
        GIT_TAG        v1.7.1
)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(whisper)


set(MODEL_NAME "ggml-tiny.en.bin")
set(MODEL_PATH "${CMAKE_BINARY_DIR}/${MODEL_NAME}")
set(MODEL_URL "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/${MODEL_NAME}")

if(NOT EXISTS "${MODEL_PATH}")
    message(STATUS "Downloading Whisper model: ${MODEL_NAME}...")
    file(DOWNLOAD
            "${MODEL_URL}"
            "${MODEL_PATH}"
            SHOW_PROGRESS
            STATUS DOWNLOAD_STATUS
    )
    list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
    if(NOT STATUS_CODE EQUAL 0)
        message(WARNING "Failed to download Whisper model. Error: ${DOWNLOAD_STATUS}")
    else()
        message(STATUS "Successfully downloaded Whisper model to: ${MODEL_PATH}")
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Multimedia)

qt_standard_project_setup()

add_executable(Talos
        src/main.cpp
        include/window/MainWindow.h
        src/window/MainWindow.cpp
        include/ChatWidget.h
        src/ChatWidget.cpp
        include/CaptureOverlay.h
        include/AudioRecorder.h
        include/WhisperTranscriber.h
)

find_package(PkgConfig REQUIRED)

pkg_check_modules(TESSERACT REQUIRED tesseract)
pkg_check_modules(LEPTONICA REQUIRED lept)

target_include_directories(Talos PRIVATE
        ${TESSERACT_INCLUDE_DIRS}
        ${LEPTONICA_INCLUDE_DIRS}
        ${whisper_SOURCE_DIR}
)

target_link_libraries(Talos PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        Qt6::Multimedia
        whisper
        ${TESSERACT_LIBRARIES}
        ${LEPTONICA_LIBRARIES}
)
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

set(ONNXRUNTIME_VERSION "1.17.1")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(ONNX_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-linux-x64-${ONNXRUNTIME_VERSION}.tgz")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(ONNX_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-osx-x64-${ONNXRUNTIME_VERSION}.tgz")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(ONNX_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-win-x64-${ONNXRUNTIME_VERSION}.zip")
endif()

FetchContent_Declare(
        onnxruntime_prebuilt
        URL ${ONNX_URL}
)
FetchContent_MakeAvailable(onnxruntime_prebuilt)

set(ONNXRUNTIME_INCLUDE_DIR "${onnxruntime_prebuilt_SOURCE_DIR}/include")

if(WIN32)
    set(ONNXRUNTIME_LIBRARY "${onnxruntime_prebuilt_SOURCE_DIR}/lib/onnxruntime.lib")
else()
    set(ONNXRUNTIME_LIBRARY "${onnxruntime_prebuilt_SOURCE_DIR}/lib/libonnxruntime.so")
endif()

add_library(ONNXRuntime::ONNXRuntime UNKNOWN IMPORTED)
set_target_properties(ONNXRuntime::ONNXRuntime PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}"
        IMPORTED_LOCATION "${ONNXRUNTIME_LIBRARY}"
)

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

set(MODELS_DIR "${CMAKE_BINARY_DIR}/models")
file(MAKE_DIRECTORY "${MODELS_DIR}")

set(URL_MEL "https://github.com/dscripka/openWakeWord/releases/download/v0.5.1/melspectrogram.onnx")
set(URL_EMB "https://github.com/dscripka/openWakeWord/releases/download/v0.5.1/embedding_model.onnx")
set(URL_JARVIS "https://github.com/dscripka/openWakeWord/releases/download/v0.5.1/hey_jarvis_v0.1.onnx")

set(MODEL_URLS ${URL_MEL} ${URL_EMB} ${URL_JARVIS})
set(MODEL_FILES "melspectrogram.onnx" "embedding_model.onnx" "hey_jarvis.onnx")

list(LENGTH MODEL_FILES NUM_MODELS)
math(EXPR MAX_INDEX "${NUM_MODELS} - 1")

foreach(INDEX RANGE ${MAX_INDEX})
    list(GET MODEL_FILES ${INDEX} MODEL_FILE)
    list(GET MODEL_URLS ${INDEX} FILE_URL)

    set(DEST_PATH "${MODELS_DIR}/${MODEL_FILE}")

    set(FILE_IS_VALID FALSE)
    if(EXISTS "${DEST_PATH}")
        file(SIZE "${DEST_PATH}" FILE_SIZE)
        if(FILE_SIZE GREATER 10240)
            set(FILE_IS_VALID TRUE)
        endif()
    endif()

    if(NOT FILE_IS_VALID)
        message(STATUS "Downloading openWakeWord model: ${MODEL_FILE}...")

        execute_process(
                COMMAND curl -L -f -s -S "${FILE_URL}" -o "${DEST_PATH}"
                RESULT_VARIABLE CURL_RESULT
        )

        if(NOT CURL_RESULT EQUAL 0)
            message(WARNING "Failed to download model ${MODEL_FILE} via cURL.")
        else()
            message(STATUS "Successfully downloaded ${MODEL_FILE}")
        endif()
    endif()
endforeach()


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
        include/WakeWordDetector.h
)

find_package(PkgConfig REQUIRED)

pkg_check_modules(TESSERACT REQUIRED tesseract)
pkg_check_modules(LEPTONICA REQUIRED lept)

target_include_directories(Talos PRIVATE
        ${TESSERACT_INCLUDE_DIRS}
        ${LEPTONICA_INCLUDE_DIRS}
        ${whisper_SOURCE_DIR}
        ${ONNXRUNTIME_INCLUDE_DIR}
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
        ONNXRuntime::ONNXRuntime
)
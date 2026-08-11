cmake_minimum_required(VERSION 3.20)
project(Talos)

# Setup standard Qt target settings
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Multimedia WebEngineWidgets WebEngineCore )
qt_standard_project_setup()

# -----------------------------------------------------------------------------
# 1. Whisper.cpp Integration
# -----------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
        whisper
        GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
        GIT_TAG        v1.7.1
)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# Enable Metal acceleration automatically on macOS
if(APPLE)
    set(WHISPER_METAL ON CACHE BOOL "" FORCE)
endif()

FetchContent_MakeAvailable(whisper)

# -----------------------------------------------------------------------------
# 2. Cross-Platform ONNX Runtime Prebuilt Resolver
# -----------------------------------------------------------------------------
set(ONNXRUNTIME_VERSION "1.17.1")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(ONNX_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-linux-x64-${ONNXRUNTIME_VERSION}.tgz")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(ONNX_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-osx-arm64-${ONNXRUNTIME_VERSION}.tgz")
    else()
        set(ONNX_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-osx-x64-${ONNXRUNTIME_VERSION}.tgz")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(ONNX_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-win-x64-${ONNXRUNTIME_VERSION}.zip")
endif()

FetchContent_Declare(
        onnxruntime_prebuilt
        URL ${ONNX_URL}
)
FetchContent_MakeAvailable(onnxruntime_prebuilt)

set(ONNXRUNTIME_INCLUDE_DIR "${onnxruntime_prebuilt_SOURCE_DIR}/include")

# Match dynamic library extensions per platform
if(WIN32)
    set(ONNXRUNTIME_LIBRARY "${onnxruntime_prebuilt_SOURCE_DIR}/lib/onnxruntime.lib")
elseif(APPLE)
    set(ONNXRUNTIME_LIBRARY "${onnxruntime_prebuilt_SOURCE_DIR}/lib/libonnxruntime.dylib")
else()
    set(ONNXRUNTIME_LIBRARY "${onnxruntime_prebuilt_SOURCE_DIR}/lib/libonnxruntime.so")
endif()

add_library(ONNXRuntime::ONNXRuntime UNKNOWN IMPORTED)
set_target_properties(ONNXRuntime::ONNXRuntime PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}"
        IMPORTED_LOCATION "${ONNXRUNTIME_LIBRARY}"
)

# -----------------------------------------------------------------------------
# 3. Model Downloader
# -----------------------------------------------------------------------------
set(MODEL_NAME "ggml-tiny.en.bin")
set(MODEL_PATH "${CMAKE_BINARY_DIR}/${MODEL_NAME}")
set(MODEL_URL "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/${MODEL_NAME}")

if(NOT EXISTS "${MODEL_PATH}")
    message(STATUS "Downloading Whisper model: ${MODEL_NAME}...")
    file(DOWNLOAD "${MODEL_URL}" "${MODEL_PATH}" SHOW_PROGRESS STATUS DOWNLOAD_STATUS)
endif()

set(MODELS_DIR "${CMAKE_BINARY_DIR}/models")
file(MAKE_DIRECTORY "${MODELS_DIR}")

set(MODEL_URLS
        "https://github.com/dscripka/openWakeWord/releases/download/v0.5.1/melspectrogram.onnx"
        "https://github.com/dscripka/openWakeWord/releases/download/v0.5.1/embedding_model.onnx"
        "https://github.com/dscripka/openWakeWord/releases/download/v0.5.1/hey_jarvis_v0.1.onnx"
)
set(MODEL_FILES "melspectrogram.onnx" "embedding_model.onnx" "hey_jarvis.onnx")

list(LENGTH MODEL_FILES NUM_MODELS)
math(EXPR MAX_INDEX "${NUM_MODELS} - 1")

foreach(INDEX RANGE ${MAX_INDEX})
    list(GET MODEL_FILES ${INDEX} MODEL_FILE)
    list(GET MODEL_URLS ${INDEX} FILE_URL)
    set(DEST_PATH "${MODELS_DIR}/${MODEL_FILE}")

    if(NOT EXISTS "${DEST_PATH}")
        message(STATUS "Downloading openWakeWord model: ${MODEL_FILE}...")
        file(DOWNLOAD "${FILE_URL}" "${DEST_PATH}" SHOW_PROGRESS)
    endif()
endforeach()

# -----------------------------------------------------------------------------
# 4. OCR Libraries (Tesseract / Leptonica)
# -----------------------------------------------------------------------------
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(TESSERACT tesseract)
    pkg_check_modules(LEPTONICA lept)
endif()

# -----------------------------------------------------------------------------
# 5. Executable Target & Libraries
# -----------------------------------------------------------------------------
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
        include/HoleAwareTextLayout.h
        src/HoleAwareTextLayout.cpp
)

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
        Qt6::WebEngineWidgets
        Qt6::WebEngineCore
        whisper
        ONNXRuntime::ONNXRuntime
        ${TESSERACT_LIBRARIES}
        ${LEPTONICA_LIBRARIES}
)
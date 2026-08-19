cmake_minimum_required(VERSION 3.20)
project(Talos)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# Setup standard Qt target settings
find_package(Qt6 REQUIRED COMPONENTS
        Core
        Gui
        Widgets
        Network
        Multimedia
        WebEngineWidgets
        WebEngineCore
        WebChannel
        TextToSpeech
)
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
set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
if(APPLE)
    set(WHISPER_METAL ON CACHE BOOL "" FORCE)
endif()
FetchContent_MakeAvailable(whisper)

# -----------------------------------------------------------------------------
# 2. Cross-Platform ONNX Runtime Prebuilt Resolver
#    (kept for openWakeWord and other components)
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

if(WIN32)
    set(ONNXRUNTIME_LIBRARY "${onnxruntime_prebuilt_SOURCE_DIR}/lib/onnxruntime.lib")
    set(ONNXRUNTIME_DLL     "${onnxruntime_prebuilt_SOURCE_DIR}/lib/onnxruntime.dll")
elseif(APPLE)
    set(ONNXRUNTIME_LIBRARY "${onnxruntime_prebuilt_SOURCE_DIR}/lib/libonnxruntime.dylib")
    file(GLOB ONNXRUNTIME_DYLIBS "${onnxruntime_prebuilt_SOURCE_DIR}/lib/libonnxruntime*.dylib")
else()
    set(ONNXRUNTIME_LIBRARY "${onnxruntime_prebuilt_SOURCE_DIR}/lib/libonnxruntime.so")
    file(GLOB ONNXRUNTIME_SOLIBS "${onnxruntime_prebuilt_SOURCE_DIR}/lib/libonnxruntime.so*")
endif()

add_library(ONNXRuntime::ONNXRuntime UNKNOWN IMPORTED)
set_target_properties(ONNXRuntime::ONNXRuntime PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}"
        IMPORTED_LOCATION             "${ONNXRUNTIME_LIBRARY}"
)

# -----------------------------------------------------------------------------
# 3. Model Downloader (Whisper + openWakeWord only; Kokoro now served externally)
# -----------------------------------------------------------------------------
set(MODELS_DIR "${CMAKE_BINARY_DIR}/models")
file(MAKE_DIRECTORY "${MODELS_DIR}")

function(download_model url dest description)
    set(should_download FALSE)
    if(NOT EXISTS "${dest}")
        set(should_download TRUE)
    else()
        file(SIZE "${dest}" file_size)
        if(file_size EQUAL 0)
            set(should_download TRUE)
        endif()
    endif()

    if(should_download)
        message(STATUS "Downloading ${description}...")
        file(DOWNLOAD "${url}" "${dest}"
                SHOW_PROGRESS
                STATUS download_status
                TIMEOUT 300)
        list(GET download_status 0 status_code)
        list(GET download_status 1 error_message)
        if(NOT status_code EQUAL 0)
            message(WARNING "Failed to download ${description}: ${error_message}")
            message(WARNING "URL: ${url}")
            message(WARNING "You may need to download manually and place it at: ${dest}")
        else()
            file(SIZE "${dest}" file_size)
            if(file_size EQUAL 0)
                message(WARNING "Downloaded ${description} is empty (0 bytes)")
                message(WARNING "URL: ${url}")
                message(WARNING "Please download manually from the URL above")
            else()
                message(STATUS "Successfully downloaded ${description} (${file_size} bytes)")
            endif()
        endif()
    else()
        file(SIZE "${dest}" file_size)
        message(STATUS "${description} already exists (${file_size} bytes)")
    endif()
endfunction()

# A. Whisper Model
set(MODEL_NAME "ggml-tiny.en.bin")
set(MODEL_PATH "${CMAKE_BINARY_DIR}/${MODEL_NAME}")
set(MODEL_URL  "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/${MODEL_NAME}")
download_model("${MODEL_URL}" "${MODEL_PATH}" "Whisper model ${MODEL_NAME}")

# B. openWakeWord Models
set(OWW_URLS
        "https://github.com/dscripka/openWakeWord/releases/download/v0.5.1/melspectrogram.onnx"
        "https://github.com/dscripka/openWakeWord/releases/download/v0.5.1/embedding_model.onnx"
        "https://github.com/dscripka/openWakeWord/releases/download/v0.5.1/hey_jarvis_v0.1.onnx"
)
set(OWW_FILES "melspectrogram.onnx" "embedding_model.onnx" "hey_jarvis.onnx")
list(LENGTH OWW_FILES NUM_OWW_MODELS)
math(EXPR MAX_OWW_INDEX "${NUM_OWW_MODELS} - 1")
foreach(INDEX RANGE ${MAX_OWW_INDEX})
    list(GET OWW_FILES ${INDEX} MODEL_FILE)
    list(GET OWW_URLS  ${INDEX} FILE_URL)
    set(DEST_PATH "${MODELS_DIR}/${MODEL_FILE}")
    download_model("${FILE_URL}" "${DEST_PATH}" "openWakeWord model ${MODEL_FILE}")
endforeach()

# -----------------------------------------------------------------------------
# 4. OCR Libraries (Tesseract / Leptonica) with Fallbacks
# -----------------------------------------------------------------------------
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(TESSERACT tesseract)
    pkg_check_modules(LEPTONICA lept)
endif()

if(NOT TESSERACT_FOUND)
    find_library(TESSERACT_LIBRARIES NAMES tesseract)
    find_path(TESSERACT_INCLUDE_DIRS NAMES tesseract/baseapi.h)
endif()
if(NOT LEPTONICA_FOUND)
    find_library(LEPTONICA_LIBRARIES NAMES lept leptonica)
    find_path(LEPTONICA_INCLUDE_DIRS NAMES leptonica/allheaders.h)
endif()

# -----------------------------------------------------------------------------
# 5. Executable Target & Linkage
# -----------------------------------------------------------------------------
qt_add_executable(Talos
        src/main.cpp
        include/MainWindow.h
        src/MainWindow.cpp
        include/ChatWidget.h
        src/ChatWidget.cpp
        include/CaptureOverlay.h
        src/CaptureOverlay.cpp
        include/AudioRecorder.h
        include/WhisperTranscriber.h
        include/WakeWordDetector.h
        include/OcrService.h
        src/OcrService.cpp
        resources.qrc
        src/ChatBackend.cpp
        include/ChatBackend.h
        src/CodeWidget.cpp
        include/CodeWidget.h
        src/CodeBackend.cpp
        include/CodeBackend.h
        src/StreamEventParser.cpp
        include/StreamEventParser.h
        src/ResponseProcessor.cpp
        include/ResponseProcessor.h
        src/LLMPromptBuilder.cpp
        include/LLMPromptBuilder.h
        src/ApiStreamClient.cpp
        include/ApiStreamClient.h
        src/EditorCommandExecutor.cpp
        include/EditorCommandExecutor.h
        src/TtsManager.cpp
        include/TtsManager.h
        src/ProjectModel.cpp
        include/ProjectModel.h
        src/CodingAgent.cpp
        include/CodingAgent.h
)

qt_add_resources(Talos "widget_resources"
        PREFIX "/"
        FILES
        style.css
        widgets/chat/chat.html
        widgets/chat/chat.css
        widgets/chat/chat.js
        widgets/code/code.html
        widgets/code/code.css
        widgets/code/code.js
)

target_include_directories(Talos PRIVATE
        ${TESSERACT_INCLUDE_DIRS}
        ${LEPTONICA_INCLUDE_DIRS}
        ${whisper_SOURCE_DIR}
        ${ONNXRUNTIME_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(Talos PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        Qt6::Multimedia
        Qt6::WebEngineWidgets
        Qt6::WebEngineCore
        Qt6::WebChannel
        Qt6::TextToSpeech
        whisper
        ONNXRuntime::ONNXRuntime
        ${TESSERACT_LIBRARIES}
        ${LEPTONICA_LIBRARIES}
)

# Ensure the executable can find the ONNX Runtime shared library at runtime
set_target_properties(Talos PROPERTIES
        BUILD_RPATH   "$ORIGIN;${onnxruntime_prebuilt_SOURCE_DIR}/lib"
        INSTALL_RPATH "$ORIGIN"
)

# Copy web assets next to the binary
add_custom_command(
        TARGET Talos POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/style.css"
        "$<TARGET_FILE_DIR:Talos>/style.css"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/widgets/chat/chat.html"
        "$<TARGET_FILE_DIR:Talos>/chat.html"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/widgets/chat/chat.css"
        "$<TARGET_FILE_DIR:Talos>/chat.css"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/widgets/chat/chat.js"
        "$<TARGET_FILE_DIR:Talos>/code.js"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/widgets/code/code.html"
        "$<TARGET_FILE_DIR:Talos>/code.html"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/widgets/code/code.css"
        "$<TARGET_FILE_DIR:Talos>/code.css"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/widgets/code/code.js"
        "$<TARGET_FILE_DIR:Talos>/code_app.js"
)

# Copy models to build directory
add_custom_command(
        TARGET Talos POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${MODELS_DIR}"
        "$<TARGET_FILE_DIR:Talos>/models"
)

# Deploy the correct ONNX Runtime shared library next to the executable
if(WIN32)
    add_custom_command(
            TARGET Talos POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNXRUNTIME_DLL}"
            "$<TARGET_FILE_DIR:Talos>/onnxruntime.dll"
    )
elseif(APPLE)
    foreach(dylib ${ONNXRUNTIME_DYLIBS})
        add_custom_command(
                TARGET Talos POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${dylib}"
                "$<TARGET_FILE_DIR:Talos>/"
        )
    endforeach()
else() # Linux
    foreach(solib ${ONNXRUNTIME_SOLIBS})
        add_custom_command(
                TARGET Talos POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${solib}"
                "$<TARGET_FILE_DIR:Talos>/"
        )
    endforeach()
endif()
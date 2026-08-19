cmake_minimum_required(VERSION 3.22)
project(GestureRack VERSION 0.6.0 LANGUAGES C CXX)

set(GESTURERACK_JUCE_VERSION "8.0.15" CACHE STRING "JUCE version fetched when no local/system JUCE is available")
option(GESTURERACK_FETCH_JUCE "Download JUCE automatically when JUCE/ and a system JUCE package are unavailable" ON)

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(STATUS "GestureRack: using local JUCE directory")
    add_subdirectory(JUCE)
else()
    find_package(JUCE CONFIG QUIET)
    if (NOT JUCE_FOUND)
        if (NOT GESTURERACK_FETCH_JUCE)
            message(FATAL_ERROR
                "JUCE was not found. Put JUCE in ./JUCE, install JUCE and set CMAKE_PREFIX_PATH, "
                "or configure with -DGESTURERACK_FETCH_JUCE=ON.")
        endif()
        include(FetchContent)
        message(STATUS "GestureRack: fetching JUCE ${GESTURERACK_JUCE_VERSION}")
        FetchContent_Declare(
            JUCE
            URL "https://github.com/juce-framework/JUCE/archive/refs/tags/${GESTURERACK_JUCE_VERSION}.zip"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()
endif()

# PRISIM uses the hinted static Inter 4.1 TTFs from the official rsms/inter release.
# Fetch the exact release archive at configure time, verify its checksum, and compile
# only the four required faces into the plugin binary. Runtime never depends on an OS
# font installation and therefore never silently falls back to Segoe UI/SF/Arial/etc.
set(GESTURERACK_INTER_VERSION "4.1" CACHE STRING "Pinned Inter release used by the Gesture Rack UI")
set(GESTURERACK_INTER_ARCHIVE_SHA256
    "9883FDD4A49D4FB66BD8177BA6625EF9A64AA45899767DDE3D36AA425756B11E")
set(GESTURERACK_INTER_CACHE_DIR "${CMAKE_BINARY_DIR}/_deps/gesturerack-inter-${GESTURERACK_INTER_VERSION}")
set(GESTURERACK_INTER_ARCHIVE "${GESTURERACK_INTER_CACHE_DIR}/Inter-${GESTURERACK_INTER_VERSION}.zip")
set(GESTURERACK_INTER_ROOT "${GESTURERACK_INTER_CACHE_DIR}/extracted")
set(GESTURERACK_INTER_TTF_DIR "${GESTURERACK_INTER_ROOT}/extras/ttf")
set(GESTURERACK_INTER_FONTS
    "${GESTURERACK_INTER_TTF_DIR}/Inter-Regular.ttf"
    "${GESTURERACK_INTER_TTF_DIR}/Inter-SemiBold.ttf"
    "${GESTURERACK_INTER_TTF_DIR}/Inter-Bold.ttf"
    "${GESTURERACK_INTER_TTF_DIR}/InterDisplay-ExtraBold.ttf")

set(_gesturerack_inter_ready TRUE)
foreach(_font IN LISTS GESTURERACK_INTER_FONTS)
    if (NOT EXISTS "${_font}")
        set(_gesturerack_inter_ready FALSE)
    endif()
endforeach()

if (NOT _gesturerack_inter_ready)
    file(MAKE_DIRECTORY "${GESTURERACK_INTER_CACHE_DIR}")

    if (EXISTS "${GESTURERACK_INTER_ARCHIVE}")
        file(SHA256 "${GESTURERACK_INTER_ARCHIVE}" _gesturerack_inter_existing_sha256)
        if (NOT _gesturerack_inter_existing_sha256 STREQUAL GESTURERACK_INTER_ARCHIVE_SHA256)
            message(STATUS "GestureRack: cached Inter archive checksum changed; downloading the pinned release again")
            file(REMOVE "${GESTURERACK_INTER_ARCHIVE}")
        endif()
    endif()

    if (NOT EXISTS "${GESTURERACK_INTER_ARCHIVE}")
        message(STATUS "GestureRack: fetching pinned Inter ${GESTURERACK_INTER_VERSION} static TTFs")
        file(DOWNLOAD
            "https://github.com/rsms/inter/releases/download/v${GESTURERACK_INTER_VERSION}/Inter-${GESTURERACK_INTER_VERSION}.zip"
            "${GESTURERACK_INTER_ARCHIVE}"
            EXPECTED_HASH "SHA256=${GESTURERACK_INTER_ARCHIVE_SHA256}"
            TLS_VERIFY ON
            SHOW_PROGRESS
            STATUS _gesturerack_inter_download_status)
        list(GET _gesturerack_inter_download_status 0 _gesturerack_inter_download_code)
        list(GET _gesturerack_inter_download_status 1 _gesturerack_inter_download_message)
        if (NOT _gesturerack_inter_download_code EQUAL 0)
            message(FATAL_ERROR
                "GestureRack: failed to download pinned Inter ${GESTURERACK_INTER_VERSION}: "
                "${_gesturerack_inter_download_message}")
        endif()
    endif()

    file(REMOVE_RECURSE "${GESTURERACK_INTER_ROOT}")
    file(MAKE_DIRECTORY "${GESTURERACK_INTER_ROOT}")
    file(ARCHIVE_EXTRACT INPUT "${GESTURERACK_INTER_ARCHIVE}" DESTINATION "${GESTURERACK_INTER_ROOT}")
endif()

foreach(_font IN LISTS GESTURERACK_INTER_FONTS)
    if (NOT EXISTS "${_font}")
        message(FATAL_ERROR "GestureRack: required PRISIM font asset is missing after extraction: ${_font}")
    endif()
endforeach()

juce_add_binary_data(GestureRackFonts
    SOURCES
        ${GESTURERACK_INTER_FONTS})

if (APPLE)
    set(GESTURE_RACK_FORMATS VST3 AU Standalone)
else()
    set(GESTURE_RACK_FORMATS VST3 Standalone)
endif()

juce_add_plugin(GestureRack
    COMPANY_NAME "Gesture Rack"
    BUNDLE_ID "com.gesturerack.audio"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE GsRK
    PLUGIN_CODE GsR1
    FORMATS ${GESTURE_RACK_FORMATS}
    PRODUCT_NAME "Gesture Rack"
    VERSION ${PROJECT_VERSION})

juce_generate_juce_header(GestureRack)

target_sources(GestureRack PRIVATE
    Source/GestureTypes.h
    Source/ControlGesture.h
    Source/DualHandVisionTypes.h
    Source/GestureBinding.h
    Source/GestureBinding.cpp
    Source/GestureMappingEngine.h
    Source/GestureMappingEngine.cpp
    Source/RightGestureRuntime.h
    Source/RightGestureRuntime.cpp
    Source/ParameterLearnManager.h
    Source/ParameterLearnManager.cpp
    Source/ParameterInspector.h
    Source/ParameterInspector.cpp
    Source/VisionReceiver.h
    Source/VisionReceiver.cpp
    Source/VisionFrameReader.h
    Source/VisionFrameReader.cpp
    Source/HostedPluginTopology.h
    Source/PluginSlot.h
    Source/PluginSlot.cpp
    Source/RackGraphManager.h
    Source/RackGraphManager.cpp
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/RackInteraction.cpp
    Source/PluginCategory.h
    Source/PluginCategory.cpp
    Source/PluginBrowser.h
    Source/PluginBrowser.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp)

target_compile_features(GestureRack PRIVATE cxx_std_20)
if (MSVC)
    target_compile_options(GestureRack PRIVATE /utf-8 /MP1)
endif()

target_compile_definitions(GestureRack
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_PLUGINHOST_VST3=1
        $<$<PLATFORM_ID:Darwin>:JUCE_PLUGINHOST_AU=1>)

target_link_libraries(GestureRack
    PRIVATE
        GestureRackFonts
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

juce_add_console_app(GestureRackScanner PRODUCT_NAME "GestureRackScanner")
juce_generate_juce_header(GestureRackScanner)
target_sources(GestureRackScanner PRIVATE
    Source/PluginCategory.h
    Source/PluginCategory.cpp
    Source/ScannerMain.cpp)
target_compile_features(GestureRackScanner PRIVATE cxx_std_20)
if (MSVC)
    target_compile_options(GestureRackScanner PRIVATE /utf-8 /MP1)
endif()
target_compile_definitions(GestureRackScanner
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_PLUGINHOST_VST3=1
        $<$<PLATFORM_ID:Darwin>:JUCE_PLUGINHOST_AU=1>)
target_link_libraries(GestureRackScanner
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if (TARGET GestureRack_VST3)
    add_dependencies(GestureRack_VST3 GestureRackScanner)
    add_custom_command(TARGET GestureRack_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:GestureRackScanner>
                $<TARGET_FILE_DIR:GestureRack_VST3>
        COMMENT "Bundling isolated GestureRackScanner beside the VST3 binary")
endif()

if (TARGET GestureRack_Standalone)
    add_dependencies(GestureRack_Standalone GestureRackScanner)
    add_custom_command(TARGET GestureRack_Standalone POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:GestureRackScanner>
                $<TARGET_FILE_DIR:GestureRack_Standalone>
        COMMENT "Bundling isolated GestureRackScanner beside the standalone app")
endif()

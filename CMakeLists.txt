cmake_minimum_required(VERSION 3.22)
project(GestureRack VERSION 0.1.0 LANGUAGES C CXX)

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
    PRODUCT_NAME "Gesture Rack")

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
    Source/GestureBypassWrapper.h
    Source/GestureBypassWrapper.cpp
    Source/PluginSlot.h
    Source/PluginSlot.cpp
    Source/RackGraphManager.h
    Source/RackGraphManager.cpp
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/RackInteraction.cpp
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
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

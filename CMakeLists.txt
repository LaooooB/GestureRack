cmake_minimum_required(VERSION 3.22)
project(GestureRack VERSION 0.5.0 LANGUAGES C CXX)

set(GESTURERACK_JUCE_VERSION "8.0.15" CACHE STRING "JUCE version fetched when no local/system JUCE is available")
option(GESTURERACK_FETCH_JUCE "Download JUCE automatically when JUCE/ and a system JUCE package are unavailable" ON)

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(STATUS "GestureRack: using local JUCE directory")
    add_subdirectory(JUCE)
else()
    find_package(JUCE CONFIG QUIET)
    if (NOT JUCE_FOUND)
        if (NOT GESTURERACK_FETCH_JUCE)
            message(FATAL_ERROR "JUCE was not found. Add JUCE/ beside this CMakeLists.txt, install a JUCE package, or enable GESTURERACK_FETCH_JUCE.")
        endif()
        include(FetchContent)
        FetchContent_Declare(
            juce
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG ${GESTURERACK_JUCE_VERSION}
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(juce)
    endif()
endif()

set(GESTURERACK_FORMATS VST3 Standalone)
if(APPLE)
    set(GESTURERACK_FORMATS VST3 AU Standalone)
endif()

juce_add_plugin(GestureRack
    COMPANY_NAME "Gesture Rack"
    BUNDLE_ID "com.gesturerack.plugin"
    PLUGIN_MANUFACTURER_CODE GsRK
    PLUGIN_CODE GsR1
    FORMATS ${GESTURERACK_FORMATS}
    PRODUCT_NAME "Gesture Rack"
    PLUGIN_NAME "Gesture Rack"
    DESCRIPTION "Gesture-controlled VST3 plug-in rack"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(GestureRack)

set(GESTURERACK_SOURCES
    Source/GestureBinding.cpp
    Source/GestureBypassWrapper.cpp
    Source/GestureMappingEngine.cpp
    Source/ParameterInspector.cpp
    Source/ParameterLearnManager.cpp
    Source/PluginBrowser.cpp
    Source/PluginCategory.cpp
    Source/PluginEditor.cpp
    Source/PluginProcessor.cpp
    Source/PluginSlot.cpp
    Source/RackGraphManager.cpp
    Source/RackInteraction.cpp
    Source/RightGestureRuntime.cpp
    Source/UiTheme.cpp
    Source/VisionReceiver.cpp
)

target_sources(GestureRack PRIVATE ${GESTURERACK_SOURCES})

target_compile_features(GestureRack PRIVATE cxx_std_17)

target_compile_definitions(GestureRack
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_PLUGINHOST_VST3=1
)

if(APPLE)
    target_compile_definitions(GestureRack PUBLIC JUCE_PLUGINHOST_AU=1)
endif()

target_link_libraries(GestureRack
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

juce_add_console_app(GestureRackScanner
    PRODUCT_NAME "GestureRackScanner"
)

juce_generate_juce_header(GestureRackScanner)

target_sources(GestureRackScanner PRIVATE
    Source/ScannerMain.cpp
    Source/PluginCategory.cpp
)

target_compile_features(GestureRackScanner PRIVATE cxx_std_17)

target_compile_definitions(GestureRackScanner
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_PLUGINHOST_VST3=1
)

if(APPLE)
    target_compile_definitions(GestureRackScanner PUBLIC JUCE_PLUGINHOST_AU=1)
endif()

target_link_libraries(GestureRackScanner
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if(WIN32)
    add_custom_command(TARGET GestureRackScanner POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:GestureRack_VST3>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:GestureRackScanner>"
            "$<TARGET_FILE_DIR:GestureRack_VST3>/GestureRackScanner.exe"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:GestureRack_Standalone>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:GestureRackScanner>"
            "$<TARGET_FILE_DIR:GestureRack_Standalone>/GestureRackScanner.exe"
    )
elseif(APPLE)
    add_custom_command(TARGET GestureRackScanner POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:GestureRack_VST3>/Contents/MacOS"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:GestureRackScanner>"
            "$<TARGET_BUNDLE_DIR:GestureRack_VST3>/Contents/MacOS/GestureRackScanner"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:GestureRack_Standalone>/Contents/MacOS"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:GestureRackScanner>"
            "$<TARGET_BUNDLE_DIR:GestureRack_Standalone>/Contents/MacOS/GestureRackScanner"
    )
endif()

add_dependencies(GestureRack_VST3 GestureRackScanner)
add_dependencies(GestureRack_Standalone GestureRackScanner)
if(APPLE)
    add_dependencies(GestureRack_AU GestureRackScanner)
endif()

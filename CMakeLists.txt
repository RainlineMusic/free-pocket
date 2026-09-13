# Free Pocket - build definition.
cmake_minimum_required(VERSION 3.22)

project(FreePocket VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# DSP core tests: no JUCE, no audio device, just a C++17 compiler.
# ---------------------------------------------------------------------------
enable_testing()

add_executable(FreePocketTests Tests/dsp_core_test.cpp)
target_compile_features(FreePocketTests PRIVATE cxx_std_17)

if(MSVC)
  target_compile_options(FreePocketTests PRIVATE /W4)
else()
  target_compile_options(FreePocketTests PRIVATE -Wall -Wextra)
endif()

add_test(NAME FreePocketDspCore COMMAND FreePocketTests)

# ---------------------------------------------------------------------------
# Plugin targets. JUCE is fetched by CI, never vendored into this repository.
# ---------------------------------------------------------------------------
set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE" CACHE PATH "Path to the JUCE source tree")

if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
  message(WARNING "JUCE not found at ${JUCE_DIR}: only the FreePocketTests target will be built.")
  return()
endif()

add_subdirectory("${JUCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)

juce_add_plugin(FreePocket
  PRODUCT_NAME "Free Pocket"
  COMPANY_NAME "Rainline Music"
  COMPANY_EMAIL "rainlinemusic@gmail.com"
  BUNDLE_ID com.rainlinemusic.freepocket
  PLUGIN_MANUFACTURER_CODE RnLn
  PLUGIN_CODE FrPk
  FORMATS VST3 AAX Standalone
  VST3_CATEGORIES Fx Dynamics
  AAX_CATEGORY AAX_ePlugInCategory_Dynamics
  AAX_IDENTIFIER com.rainlinemusic.freepocket
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE)

juce_generate_juce_header(FreePocket)

target_sources(FreePocket PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp)

target_compile_definitions(FreePocket PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_REPORT_APP_USAGE=0)

target_link_libraries(FreePocket
  PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

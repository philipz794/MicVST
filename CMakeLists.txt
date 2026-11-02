cmake_minimum_required(VERSION 3.22)
project(MicVST VERSION 1.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Quelldateien sind UTF-8 (deutsche Kommentare mit Umlauten). /utf-8 lässt MSVC sie
# korrekt parsen und vermeidet C4819-Warnungen.
if(MSVC)
    add_compile_options(/utf-8)
    # Statische MSVC-Runtime -> voll-portable exe ohne VC++-Redistributable-Abhängigkeit.
    # Gilt auch für JUCE, weil vor FetchContent gesetzt (CMP0091/NEW ab CMake 3.15).
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.13
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# --- Shared logic library (engine, state, audio helpers) ---
add_library(MicVSTCore STATIC
    src/audio/Metering.cpp
    src/audio/GraphConnections.cpp
    src/audio/ScanCoordinator.cpp
    src/audio/Vst3ClassScan.cpp
    src/state/Persistence.cpp
    src/state/Hotkey.cpp
    src/state/PluginScanCache.cpp
    src/ui/PluginSearch.cpp
)
target_compile_features(MicVSTCore PUBLIC cxx_std_17)
target_include_directories(MicVSTCore PUBLIC src)
target_link_libraries(MicVSTCore PUBLIC
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_core
    juce::juce_data_structures
)

# --- GUI application ---
juce_add_gui_app(MicVST
    PRODUCT_NAME "MicVST"
    COMPANY_NAME "MicVST"
    ICON_BIG   "${CMAKE_CURRENT_SOURCE_DIR}/resources/iconDarkMode.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/resources/iconDarkMode.png"
)

# Beide Icon-Varianten als Binärdaten einbetten (für das theme-adaptive Tray-Icon).
juce_add_binary_data(MicVSTData SOURCES
    resources/iconDarkMode.png
    resources/iconLightMode.png
)

target_sources(MicVST PRIVATE
    src/Main.cpp
    src/audio/AudioEngine.cpp
    src/audio/PluginChain.cpp
    src/ui/MainComponent.cpp
    src/ui/DevicePanel.cpp
    src/ui/PluginListView.cpp
    src/platform/GlobalHotkeys.cpp
    src/ui/PluginPicker.cpp
    src/ui/LevelMeterComponent.cpp
    src/ui/TrayIcon.cpp
    src/state/AutostartRegistry.cpp
)
target_compile_definitions(MicVST PRIVATE
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=0
    JUCE_ASIO=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
)
target_link_libraries(MicVST PRIVATE
    MicVSTCore
    MicVSTData
    juce::juce_audio_devices
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# --- Unit tests (console app, JUCE UnitTestRunner) ---
juce_add_console_app(MicVSTTests PRODUCT_NAME "MicVSTTests")
target_sources(MicVSTTests PRIVATE
    tests/TestMain.cpp
    tests/ScanTests.cpp
    tests/PickerTests.cpp
)
target_compile_definitions(MicVSTTests PRIVATE
    JUCE_PLUGINHOST_VST3=1
)
target_link_libraries(MicVSTTests PRIVATE
    MicVSTCore
    juce::juce_audio_processors
    juce::juce_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

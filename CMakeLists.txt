cmake_minimum_required(VERSION 3.22)

project(Valved VERSION 0.8.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    # Binário universal (Mac Intel + Apple Silicon)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

# JUCE: usa uma cópia local se existir (pasta JUCE ao lado), senão baixa do GitHub
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../JUCE" "${CMAKE_BINARY_DIR}/JUCE")
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(VLV_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND VLV_FORMATS AU)
endif()

juce_add_plugin(Valved
    COMPANY_NAME "Rapha Bueno"
    COMPANY_WEBSITE ""
    BUNDLE_ID com.raphabueno.valved
    PLUGIN_MANUFACTURER_CODE Rbzb
    PLUGIN_CODE Vld1
    FORMATS ${VLV_FORMATS}
    PRODUCT_NAME "Valved"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Distortion
    AU_MAIN_TYPE kAudioUnitType_Effect)

juce_add_binary_data(ValvedAssets SOURCES
    Resources/Fonts/BarlowCondensed-Medium.ttf
    Resources/Fonts/BarlowCondensed-SemiBold.ttf
    Resources/Fonts/BarlowCondensed-Bold.ttf
    Resources/Fonts/BarlowCondensed-BlackItalic.ttf
    Resources/Fonts/RussoOne-Regular.ttf
    Resources/Fonts/MrsSaintDelafield-Regular.ttf
    Resources/Fonts/Michroma-Regular.ttf
    Resources/Fonts/Cinzel.ttf)

target_sources(Valved PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/Style.cpp
    Source/ui/AmpHead.cpp
    Source/ui/CabPage.cpp
    Source/ui/PedalsPage.cpp
    Source/ui/FxPage.cpp)

target_include_directories(Valved PRIVATE Source ThirdParty)

target_compile_definitions(Valved PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# ASIO (Windows): ativado quando a pasta "common" do ASIO SDK da Steinberg é informada.
# ASIO is a trademark and software of Steinberg Media Technologies GmbH.
set(ASIO_SDK_DIR "" CACHE PATH "Pasta 'common' do ASIO SDK (contém iasiodrv.h)")
if(WIN32 AND ASIO_SDK_DIR AND EXISTS "${ASIO_SDK_DIR}/iasiodrv.h")
    target_include_directories(Valved PRIVATE "${ASIO_SDK_DIR}")
    target_compile_definitions(Valved PUBLIC JUCE_ASIO=1)
    message(STATUS "Valved: suporte a ASIO ATIVADO (${ASIO_SDK_DIR})")
else()
    message(STATUS "Valved: compilando sem ASIO")
endif()

if(UNIX AND NOT APPLE)
    # Apenas para testes de compilação em Linux
    target_compile_definitions(Valved PUBLIC
        JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0
        JUCE_USE_XCURSOR=0 JUCE_USE_XSHM=0 JUCE_USE_XRENDER=0)
endif()

option(VLV_BUILD_TESTS "Compila o teste offline" OFF)
if(VLV_BUILD_TESTS)
    juce_add_console_app(ValvedTest PRODUCT_NAME "ValvedTest")
    target_sources(ValvedTest PRIVATE Tests/TestMain.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/ui/Style.cpp
    Source/ui/AmpHead.cpp
    Source/ui/CabPage.cpp
    Source/ui/PedalsPage.cpp
    Source/ui/FxPage.cpp)
    target_include_directories(ValvedTest PRIVATE Source ThirdParty)
    target_compile_definitions(ValvedTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_USE_XSHM=0)
    target_link_libraries(ValvedTest PRIVATE ValvedAssets juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()

target_link_libraries(Valved
    PRIVATE
        ValvedAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

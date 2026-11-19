cmake_minimum_required(VERSION 3.22)

project(Valved VERSION 0.33.0)

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
    Resources/Fonts/Cinzel.ttf
    Resources/RecUI/rec_background.jpg
    Resources/RecUI/rec_background_v2.jpg
    Resources/RecUI/rec_knob.png
    Resources/RecUI/rec_power_off.png
    Resources/RecUI/rec_standby_off.png
    Resources/RecUI/rec_led_off.png
    Resources/BlockUI/block_background.jpg
    Resources/BlockUI/block_knob.png
    Resources/BlockUI/block_knob_shadow.png
    Resources/BlockUI/block_power_off.png
    Resources/BlockUI/block_standby_off.png
    Resources/BlockUI/block_led_off.png
    Resources/CleanUI/clean_background.jpg
    Resources/CleanUI/clean_power_off.png
    Resources/CleanUI/clean_standby_off.png
    Resources/CleanUI/clean_led_off.png
    Resources/BritUI/brit_background.jpg
    Resources/BritUI/brit_knob.png
    Resources/BritUI/brit_power_off.png
    Resources/BritUI/brit_standby_off.png
    Resources/BritUI/brit_led_off.png
    Resources/FxUI/dly_body.png
    Resources/FxUI/dly_foot.png
    Resources/FxUI/dly_led_on_off.png
    Resources/FxUI/dly_led_on_on.png
    Resources/FxUI/dly_led_sync_off.png
    Resources/FxUI/dly_led_sync_on.png
    Resources/FxUI/dly_led_tap_on.png
    Resources/FxUI/dly_listled_on.png
    Resources/FxUI/dly_sync.png
    Resources/FxUI/eq_body.png
    Resources/FxUI/eq_btn_off.png
    Resources/FxUI/eq_btn_on.png
    Resources/FxUI/eq_cap.png
    Resources/FxUI/eq_foot.png
    Resources/FxUI/eq_ledoff.png
    Resources/FxUI/knob_big.png
    Resources/FxUI/knob_big_cyan.png
    Resources/FxUI/knob_cream_np.png
    Resources/FxUI/knob_cyan_big.png
    Resources/FxUI/knob_cyan_small.png
    Resources/FxUI/knob_green_big.png
    Resources/FxUI/knob_green_small.png
    Resources/FxUI/knob_silver_np.png
    Resources/FxUI/knob_yellow.png
    Resources/FxUI/pedal_blue.png
    Resources/FxUI/pedal_blue_ledoff.png
    Resources/FxUI/pedal_chorus.png
    Resources/FxUI/pedal_chorus_ledoff.png
    Resources/FxUI/pedal_gold.png
    Resources/FxUI/pedal_gold_ledoff.png
    Resources/FxUI/pedal_green.png
    Resources/FxUI/pedal_green_ledoff.png
    Resources/FxUI/pedal_yellow.png
    Resources/FxUI/pedal_yellow_ledoff.png
    Resources/FxUI/rev_body.png
    Resources/FxUI/rev_foot.png
    Resources/FxUI/rev_led_off.png
    Resources/FxUI/rev_led_on.png
    Resources/FxUI/rev_listled_on.png
    Resources/FxUI/slot_chorus.png
    Resources/FxUI/slot_thumb.png)

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

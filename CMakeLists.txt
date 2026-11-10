cmake_minimum_required(VERSION 3.24)

# ---- Datos del producto: cambiar aca nombre y marca -------------------------
set(PLUGIN_NAME      "sampler5")
set(PLUGIN_COMPANY   "MiMarca")        # marca provisoria
set(PLUGIN_MFR_CODE  "Mmrc")           # 4 caracteres, al menos una mayuscula
set(PLUGIN_CODE      "Smp5")           # 4 caracteres, exactamente una mayuscula
# -----------------------------------------------------------------------------

project(Sampler5 VERSION 1.3.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # sin DLLs de runtime

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.5
    GIT_SHALLOW ON)
FetchContent_Declare(signalsmith-stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG 88061fd4eda56a2353c5d65c74aa3f4ccf76f9da)
FetchContent_MakeAvailable(JUCE signalsmith-stretch)

# ---- Motor (sin interfaz): los mismos fuentes van al plugin y a las pruebas --
set(ENGINE_SOURCES
    Source/Engine/Analysis.cpp
    Source/Engine/Stretcher.cpp
    Source/Engine/SamplerEngine.cpp)

function(sampler_configure_target target)
    target_sources(${target} PRIVATE ${ENGINE_SOURCES})
    target_include_directories(${target} PRIVATE Source)
    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_basics
            juce::juce_audio_formats
            juce::juce_dsp
            signalsmith-stretch
        PUBLIC
            juce::juce_recommended_config_flags)
endfunction()

# ---- Plugin ------------------------------------------------------------------
juce_add_plugin(Sampler5
    COMPANY_NAME "${PLUGIN_COMPANY}"
    PLUGIN_MANUFACTURER_CODE ${PLUGIN_MFR_CODE}
    PLUGIN_CODE ${PLUGIN_CODE}
    FORMATS VST3 Standalone
    PRODUCT_NAME "${PLUGIN_NAME}"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Instrument Sampler
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

target_sources(Sampler5 PRIVATE
    Source/PluginProcessor.cpp
    Source/Look.cpp
    Source/PluginEditor.cpp)

# Tipografia propia (se genera con Tools/make_font.py) y, si existe, el escudo.
# Para poner el escudo: guardar la imagen como Assets/escudo.png y volver a compilar.
set(ASSET_FILES Assets/Sampler5Sans.ttf)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Assets/escudo.png")
    list(APPEND ASSET_FILES Assets/escudo.png)
    target_compile_definitions(Sampler5 PRIVATE SAMPLER_HAS_CREST=1)
endif()
juce_add_binary_data(SamplerAssets SOURCES ${ASSET_FILES})
set_target_properties(SamplerAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(Sampler5 PRIVATE SamplerAssets)

sampler_configure_target(Sampler5)
target_link_libraries(Sampler5
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_lto_flags)

# ---- Pruebas del motor (consola) --------------------------------------------
option(SAMPLER_BUILD_TESTS "Compilar las pruebas del motor" ON)
if(SAMPLER_BUILD_TESTS)
    juce_add_console_app(EngineTests PRODUCT_NAME "EngineTests")
    target_sources(EngineTests PRIVATE Tests/EngineTests.cpp)
    sampler_configure_target(EngineTests)

    # prueba de integracion: el procesador real del plugin, sin DAW
    add_executable(PluginTests Tests/PluginTests.cpp)
    target_include_directories(PluginTests PRIVATE Source ${juce_SOURCE_DIR}/modules)
    target_compile_definitions(PluginTests PRIVATE $<TARGET_PROPERTY:Sampler5,COMPILE_DEFINITIONS>)
    target_link_libraries(PluginTests PRIVATE Sampler5)

    # arnes para probar la interfaz con clics reales (solo desarrollo)
    add_executable(UiHarness Tests/UiHarness.cpp)
    target_include_directories(UiHarness PRIVATE Source ${juce_SOURCE_DIR}/modules)
    target_compile_definitions(UiHarness PRIVATE $<TARGET_PROPERTY:Sampler5,COMPILE_DEFINITIONS>)
    target_link_libraries(UiHarness PRIVATE Sampler5)

    # prueba con el VST3 ya compilado, cargado como lo haria un DAW
    juce_add_console_app(HostTests PRODUCT_NAME "HostTests")
    target_sources(HostTests PRIVATE Tests/HostTests.cpp)
    target_compile_definitions(HostTests PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(HostTests PRIVATE juce::juce_audio_processors)
    add_dependencies(HostTests Sampler5_VST3)

    enable_testing()
    add_test(NAME EngineTests COMMAND EngineTests)
    add_test(NAME PluginTests COMMAND PluginTests)
endif()

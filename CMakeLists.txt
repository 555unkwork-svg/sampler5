cmake_minimum_required(VERSION 3.24)

# ---- Datos del producto: cambiar aca nombre y marca -------------------------
set(PLUGIN_NAME      "Sampler16")      # nombre provisorio
set(PLUGIN_COMPANY   "MiMarca")        # marca provisoria
set(PLUGIN_MFR_CODE  "Mmrc")           # 4 caracteres, al menos una mayuscula
set(PLUGIN_CODE      "Smp6")           # 4 caracteres, exactamente una mayuscula
# -----------------------------------------------------------------------------

project(Sampler16 VERSION 0.1.0 LANGUAGES C CXX)

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
juce_add_plugin(Sampler16
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

target_sources(Sampler16 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

sampler_configure_target(Sampler16)
target_link_libraries(Sampler16
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
    target_compile_definitions(PluginTests PRIVATE $<TARGET_PROPERTY:Sampler16,COMPILE_DEFINITIONS>)
    target_link_libraries(PluginTests PRIVATE Sampler16)

    enable_testing()
    add_test(NAME EngineTests COMMAND EngineTests)
    add_test(NAME PluginTests COMMAND PluginTests)
endif()

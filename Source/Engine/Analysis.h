#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace eng
{
constexpr int kNumPads = 16;

struct Onset
{
    juce::int64 position = 0;   // en muestras del audio original
    float strength = 0.0f;
};

struct Analysis
{
    std::vector<Onset> onsets;  // ordenados por tiempo
    double bpm = 0.0;           // 0 = no se pudo estimar
    int keyRoot = -1;           // 0 = Do ... 11 = Si; -1 = no se pudo estimar
    bool keyMinor = false;
};

/** Detecta golpes (transientes) y estima el tempo. Pensado para correr fuera del hilo de audio. */
Analysis analyse (const juce::AudioBuffer<float>& audio, double sampleRate);

enum class SliceMode { transients = 0, grid, equal, random, manual };

/** Devuelve hasta kNumPads puntos de corte, ordenados por tiempo.
    gridBeats: tamano de la grilla en tiempos (negras), solo para SliceMode::grid. */
std::vector<juce::int64> makeSlices (SliceMode mode, const Analysis& analysis,
                                     juce::int64 numSamples, double sampleRate,
                                     double bpm, double gridBeats, juce::int64 randomSeed,
                                     double sensitivity = 0.5);

/** Busca un tempo escrito en el nombre del archivo (ej. "loop_130_Am.wav"). 0 si no hay. */
double bpmFromFileName (const juce::String& nameWithoutExtension);

/** Busca una tonalidad escrita en el nombre del archivo (ej. "loop_130_A#m.wav"). */
bool keyFromFileName (const juce::String& nameWithoutExtension, int& root, bool& minor);

/** "A#m", "C"... */
juce::String keyName (int root, bool minor);
}

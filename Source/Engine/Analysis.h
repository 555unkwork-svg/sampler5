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
};

/** Detecta golpes (transientes) y estima el tempo. Pensado para correr fuera del hilo de audio. */
Analysis analyse (const juce::AudioBuffer<float>& audio, double sampleRate);

enum class SliceMode { transients = 0, grid, equal, random, manual };

/** Devuelve hasta kNumPads puntos de corte, ordenados por tiempo.
    gridBeats: tamano de la grilla en tiempos (negras), solo para SliceMode::grid. */
std::vector<juce::int64> makeSlices (SliceMode mode, const Analysis& analysis,
                                     juce::int64 numSamples, double sampleRate,
                                     double bpm, double gridBeats, juce::int64 randomSeed);
}

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace eng
{
/** Estira un audio completo sin cambiarle el tono (y/o lo transpone sin cambiarle la duracion).
    speed: velocidad de reproduccion (2 = el doble de rapido, la mitad de largo).
    semitones: transposicion.
    No es de tiempo real: se llama desde un hilo de fondo. */
juce::AudioBuffer<float> stretchOffline (const juce::AudioBuffer<float>& input, double sampleRate,
                                         double speed, double semitones);
}

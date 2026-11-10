#pragma once

#include "Analysis.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace eng
{
struct AudioData
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 44100.0;
};
using AudioPtr = std::shared_ptr<const AudioData>;

/** Todo lo que el hilo de audio necesita para sonar. Es inmutable: para cambiar algo
    (cortes, audio estirado) se arma uno nuevo y se intercambia. */
struct Playable
{
    AudioPtr audio;                 // audio a leer (el original o el ya estirado)
    juce::int64 sourceLength = 0;   // largo del audio original, en muestras
    double posScale = 1.0;          // muestras de `audio` por cada muestra del original
    double rateScale = 1.0;         // velocidad extra (sync o afinacion sin stretch)
    std::array<juce::int64, kNumPads> start {};   // en muestras del original
    std::array<juce::int64, kNumPads> end {};
    std::array<bool, kNumPads> used {};
};
using PlayablePtr = std::shared_ptr<const Playable>;

struct PadSettings
{
    float gainDb = 0.0f;
    float pan = 0.0f;          // -1..1
    float pitch = 0.0f;        // semitonos
    float attackMs = 1.0f;
    float releaseMs = 10.0f;
    bool reverse = false;
    bool gate = false;         // true: suena mientras se mantiene la nota
};

struct GlobalSettings
{
    float masterDb = 0.0f;
    float velocitySens = 0.5f; // 0 = siempre a tope, 1 = sigue la velocidad
    bool mono = false;
    bool keysMode = false;     // el pad elegido se toca cromatico
    int keysRoot = 60;
    int selectedPad = 0;
};

class SamplerEngine
{
public:
    static constexpr int kMaxVoices = 16;
    static constexpr int kDefaultFirstNote = 60;   // C5 en FL Studio (C3 o C4 en otros DAW)

    SamplerEngine();

    //---- hilo de audio ----
    void prepare (double sampleRate);
    void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi);

    /** Se escriben desde el hilo de audio justo antes de process(). */
    std::array<PadSettings, kNumPads> pads;
    GlobalSettings globals;

    //---- hilo de mensajes ----
    void setPlayable (PlayablePtr newPlayable);
    /** Libera audios viejos que el hilo de audio ya solto. Llamar cada tanto. */
    void purgeRetired();
    void triggerPadFromUI (int pad, bool down);
    void allNotesOff() { panic.store (true); }

    //---- cualquier hilo ----
    std::array<std::atomic<int>, kNumPads> padNote;
    std::atomic<int> learnPad { -1 };          // pad esperando una nota MIDI, o -1
    std::atomic<int> lastLearnedPad { -1 };    // lo lee la interfaz y lo vuelve a -1

    /** Posicion de reproduccion de cada pad, 0..1 del audio original; negativo si no suena. */
    std::array<std::atomic<float>, kNumPads> padPlayPos;

private:
    struct Voice
    {
        bool active = false;
        int pad = 0;
        int noteId = -1;
        juce::uint32 age = 0;
        PlayablePtr src;
        double pos = 0.0, rate = 1.0;
        double startP = 0.0, endP = 0.0;
        bool reverse = false, gate = false;
        float keyOffset = 0.0f, velGain = 1.0f;
        float gainL = 0.0f, gainR = 0.0f;
        bool gainsSet = false;
        enum class Stage { attack, sustain, release } stage = Stage::attack;
        float env = 0.0f, envStep = 0.0f;
    };

    void noteOn (int noteId, int pad, float keyOffset, float velocity);
    void noteOff (int noteId);
    void handleMidi (const juce::MidiMessage&);
    void renderVoices (juce::AudioBuffer<float>& out, int startSample, int numSamples);
    void startRelease (Voice&, float ms);
    void updateVoiceRate (Voice&) const;

    double hostRate = 44100.0;
    std::array<Voice, kMaxVoices> voices;
    juce::uint32 ageCounter = 0;
    float lastMaster = 1.0f;

    juce::SpinLock swapLock;
    PlayablePtr shared;               // protegido por swapLock
    PlayablePtr current;              // copia del hilo de audio
    std::vector<PlayablePtr> retired; // solo hilo de mensajes

    std::atomic<juce::uint32> uiDown { 0 }, uiUp { 0 };
    std::atomic<bool> panic { false };
};
}

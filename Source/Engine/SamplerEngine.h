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

enum class FilterType { off = 0, lowPass, highPass };

struct PadSettings
{
    float gainDb = 0.0f;       // VOL
    float preGainDb = 0.0f;    // GANANCIA: refuerzo extra
    float pan = 0.0f;          // -1..1
    float pitch = 0.0f;        // semitonos
    float fine = 0.0f;         // cents
    float velAmount = 1.0f;    // VELOCIDAD: cuanto responde este pad a la fuerza del golpe
    float attackMs = 1.0f;
    float releaseMs = 10.0f;
    bool reverse = false;
    bool gate = false;         // true: suena mientras se mantiene la nota
    bool loop = false;         // repite el corte mientras se mantiene la nota
    bool mute = false;
    FilterType filter = FilterType::off;
    float cutoffHz = 20000.0f;
    float resonance = 0.0f;    // 0..1
};

struct GlobalSettings
{
    float masterDb = 0.0f;
    float pan = 0.0f;
    float velocitySens = 0.5f; // 0 = siempre a tope, 1 = sigue la velocidad
    float humanize = 0.0f;     // 0..1: variacion al azar de volumen y tiempo
    float swing = 0.5f;        // 0.5 = sin swing; hasta 0.75
    bool mono = false;
    bool chokeAll = false;     // cada pad corta a los demas
    bool invertAll = false;    // INV: el audio entero al reves
    int maxVoices = 16;
    bool midiIn = true;        // ENTRADA MIDI
    int midiChannel = 0;       // 0 = todos
    int rangeLo = 0, rangeHi = 127;
    bool aftertouchVolume = false;
    bool keysMode = false;     // el pad elegido se toca cromatico
    int keysRoot = 60;
    int selectedPad = 0;

    // datos del host para el swing (los pone el procesador en cada bloque)
    bool hostPlaying = false;
    double hostPpq = 0.0, hostBpm = 0.0;
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
        bool reverse = false, gate = false, loop = false;
        float keyOffset = 0.0f, velGain = 1.0f;
        float gainL = 0.0f, gainR = 0.0f;
        bool gainsSet = false;
        enum class Stage { attack, sustain, release } stage = Stage::attack;
        float env = 0.0f, envStep = 0.0f;
        float ic1[2] {}, ic2[2] {};   // estado del filtro
        float pressure = 0.0f;        // aftertouch 0..1
    };

    struct Event
    {
        int time = 0;        // en muestras desde el inicio del bloque
        bool on = false;
        int noteId = 0, pad = 0;
        float keyOffset = 0.0f, velocity = 1.0f;
    };

    void noteOn (int noteId, int pad, float keyOffset, float velocity);
    void noteOff (int noteId);
    void queueMidi (const juce::MidiMessage&, int time, int numSamples);
    void addEvent (const Event&, int numSamples);
    void fire (const Event&);
    void renderVoices (juce::AudioBuffer<float>& out, int startSample, int numSamples);
    void startRelease (Voice&, float ms);
    void updateVoiceRate (Voice&) const;
    int delayFor (int time);

    static constexpr int kMaxEvents = 128;
    std::array<Event, kMaxEvents> blockEvents, pending;
    int numBlockEvents = 0, numPending = 0;
    std::array<int, 128> noteDelay {};
    juce::Random random;
    float lastPanL = 1.0f, lastPanR = 1.0f;

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

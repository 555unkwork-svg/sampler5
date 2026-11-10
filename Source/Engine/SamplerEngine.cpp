#include "SamplerEngine.h"

#include <cmath>

namespace eng
{
namespace
{
constexpr float kFastFadeMs = 5.0f;
constexpr int kUiNoteBase = 1000;

inline float readSample (const float* data, int length, int index) noexcept
{
    return (index >= 0 && index < length) ? data[index] : 0.0f;
}

/** Interpolacion Hermite de 4 puntos. */
inline float hermite (const float* data, int length, double pos) noexcept
{
    const int i = (int) std::floor (pos);
    const float t = (float) (pos - (double) i);
    const float y0 = readSample (data, length, i - 1);
    const float y1 = readSample (data, length, i);
    const float y2 = readSample (data, length, i + 1);
    const float y3 = readSample (data, length, i + 2);
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}
}

SamplerEngine::SamplerEngine()
{
    for (int i = 0; i < kNumPads; ++i)
    {
        padNote[(size_t) i].store (kDefaultFirstNote + i);
        padPlayPos[(size_t) i].store (-1.0f);
    }
}

void SamplerEngine::prepare (double sampleRate)
{
    hostRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    for (auto& v : voices)
    {
        v.active = false;
        v.src.reset();   // no es la ultima referencia: `shared` o `retired` la mantienen viva
    }
}

void SamplerEngine::setPlayable (PlayablePtr newPlayable)
{
    PlayablePtr old;
    {
        const juce::SpinLock::ScopedLockType lock (swapLock);
        old = std::move (shared);
        shared = std::move (newPlayable);
    }
    if (old != nullptr)
        retired.push_back (std::move (old));
    purgeRetired();
}

void SamplerEngine::purgeRetired()
{
    // un Playable retirado se puede borrar cuando nadie mas lo usa (use_count == 1 es el nuestro)
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [] (const PlayablePtr& p) { return p.use_count() <= 1; }),
                   retired.end());
}

void SamplerEngine::triggerPadFromUI (int pad, bool down)
{
    if (pad < 0 || pad >= kNumPads)
        return;
    (down ? uiDown : uiUp).fetch_or (1u << pad);
}

void SamplerEngine::startRelease (Voice& v, float ms)
{
    if (v.stage == Voice::Stage::release && v.envStep > 0.0f)
    {
        // ya esta soltando: solo aceleramos si el nuevo tiempo es mas corto
        const float samples = std::max (1.0f, ms * 0.001f * (float) hostRate);
        v.envStep = std::max (v.envStep, v.env / samples);
        return;
    }
    v.stage = Voice::Stage::release;
    const float samples = std::max (1.0f, ms * 0.001f * (float) hostRate);
    v.envStep = std::max (1.0e-6f, v.env / samples);
}

void SamplerEngine::updateVoiceRate (Voice& v) const
{
    const auto& p = pads[(size_t) v.pad];
    const double semis = (double) p.pitch + (double) v.keyOffset;
    v.rate = std::pow (2.0, semis / 12.0) * (v.src->audio->sampleRate / hostRate) * v.src->rateScale;
}

void SamplerEngine::noteOn (int noteId, int pad, float keyOffset, float velocity)
{
    if (current == nullptr || current->audio == nullptr || ! current->used[(size_t) pad])
        return;

    const auto& settings = pads[(size_t) pad];

    for (auto& v : voices)
    {
        if (! v.active)
            continue;
        // en mono, cada nota nueva corta lo anterior; en poly solo se corta a si misma
        if (globals.mono || v.noteId == noteId)
            startRelease (v, kFastFadeMs);
    }

    Voice* target = nullptr;
    for (auto& v : voices)
        if (! v.active) { target = &v; break; }

    if (target == nullptr)
    {
        // robamos la voz mas vieja
        target = &voices[0];
        for (auto& v : voices)
            if (v.age < target->age)
                target = &v;
    }

    auto& v = *target;
    v.active = true;
    v.pad = pad;
    v.noteId = noteId;
    v.age = ++ageCounter;
    v.src = current;
    v.reverse = settings.reverse;
    v.gate = settings.gate;
    v.keyOffset = keyOffset;
    v.startP = (double) current->start[(size_t) pad] * current->posScale;
    v.endP = (double) current->end[(size_t) pad] * current->posScale;
    v.pos = v.reverse ? v.endP - 1.0 : v.startP;
    v.velGain = 1.0f - globals.velocitySens * (1.0f - velocity);
    v.gainsSet = false;
    v.stage = Voice::Stage::attack;
    v.env = 0.0f;
    const float attackSamples = std::max (16.0f, settings.attackMs * 0.001f * (float) hostRate);
    v.envStep = 1.0f / attackSamples;
    updateVoiceRate (v);
}

void SamplerEngine::noteOff (int noteId)
{
    for (auto& v : voices)
        if (v.active && v.noteId == noteId && v.gate && v.stage != Voice::Stage::release)
            startRelease (v, std::max (kFastFadeMs, pads[(size_t) v.pad].releaseMs));
}

void SamplerEngine::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        const int note = m.getNoteNumber();

        const int learning = learnPad.load();
        if (learning >= 0 && learning < kNumPads)
        {
            // la nota queda para este pad y se la sacamos al que la tuviera
            for (int i = 0; i < kNumPads; ++i)
                if (i != learning && padNote[(size_t) i].load() == note)
                    padNote[(size_t) i].store (-1);
            padNote[(size_t) learning].store (note);
            learnPad.store (-1);
            lastLearnedPad.store (learning);
        }

        if (globals.keysMode)
        {
            const int pad = juce::jlimit (0, kNumPads - 1, globals.selectedPad);
            noteOn (note, pad, (float) (note - globals.keysRoot), m.getFloatVelocity());
        }
        else
        {
            for (int i = 0; i < kNumPads; ++i)
                if (padNote[(size_t) i].load() == note)
                {
                    noteOn (note, i, 0.0f, m.getFloatVelocity());
                    break;
                }
        }
    }
    else if (m.isNoteOff())
    {
        noteOff (m.getNoteNumber());
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        for (auto& v : voices)
            if (v.active)
                startRelease (v, kFastFadeMs);
    }
}

void SamplerEngine::renderVoices (juce::AudioBuffer<float>& out, int startSample, int numSamples)
{
    if (numSamples <= 0)
        return;

    const int outChans = out.getNumChannels();
    float* outL = outChans > 0 ? out.getWritePointer (0, startSample) : nullptr;
    float* outR = outChans > 1 ? out.getWritePointer (1, startSample) : nullptr;
    if (outL == nullptr)
        return;

    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        const auto& settings = pads[(size_t) v.pad];
        const auto& buffer = v.src->audio->buffer;
        const int length = buffer.getNumSamples();
        const float* srcL = buffer.getReadPointer (0);
        const float* srcR = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : srcL;

        updateVoiceRate (v);

        // paneo de potencia constante
        const float gain = juce::Decibels::decibelsToGain (settings.gainDb, -60.0f) * v.velGain;
        const float angle = (juce::jlimit (-1.0f, 1.0f, settings.pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const float targetL = gain * std::cos (angle) * juce::MathConstants<float>::sqrt2;
        const float targetR = gain * std::sin (angle) * juce::MathConstants<float>::sqrt2;
        if (! v.gainsSet)
        {
            v.gainL = targetL;
            v.gainR = targetR;
            v.gainsSet = true;
        }
        const float stepL = (targetL - v.gainL) / (float) numSamples;
        const float stepR = (targetR - v.gainR) / (float) numSamples;

        const float endFadeMs = std::max (kFastFadeMs, settings.releaseMs);
        const double step = v.reverse ? -v.rate : v.rate;

        for (int i = 0; i < numSamples; ++i)
        {
            // fin del corte: no cortamos en seco, soltamos y seguimos leyendo un poquito
            if (v.stage != Voice::Stage::release
                && (v.reverse ? v.pos <= v.startP : v.pos >= v.endP))
                startRelease (v, v.gate ? kFastFadeMs : endFadeMs);

            switch (v.stage)
            {
                case Voice::Stage::attack:
                    v.env += v.envStep;
                    if (v.env >= 1.0f) { v.env = 1.0f; v.stage = Voice::Stage::sustain; }
                    break;
                case Voice::Stage::sustain:
                    break;
                case Voice::Stage::release:
                    v.env -= v.envStep;
                    break;
            }

            if (v.env <= 0.0f || v.pos < -2.0 || v.pos > (double) length + 2.0)
            {
                v.active = false;
                break;
            }

            const float l = hermite (srcL, length, v.pos) * v.env;
            const float r = hermite (srcR, length, v.pos) * v.env;
            v.gainL += stepL;
            v.gainR += stepR;

            if (outR != nullptr)
            {
                outL[i] += l * v.gainL;
                outR[i] += r * v.gainR;
            }
            else
            {
                outL[i] += 0.5f * (l * v.gainL + r * v.gainR);
            }

            v.pos += step;
        }

        if (! v.active)
            v.src.reset();   // nunca es la ultima referencia (ver purgeRetired)
    }
}

void SamplerEngine::process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi)
{
    const int numSamples = out.getNumSamples();
    out.clear();

    {
        // si la interfaz justo esta intercambiando, seguimos con lo que teniamos
        const juce::SpinLock::ScopedTryLockType lock (swapLock);
        if (lock.isLocked() && current != shared)
            current = shared;
    }

    if (panic.exchange (false))
        for (auto& v : voices)
            if (v.active)
                startRelease (v, kFastFadeMs);

    const auto down = uiDown.exchange (0);
    const auto up = uiUp.exchange (0);
    for (int i = 0; i < kNumPads; ++i)
    {
        if ((down & (1u << i)) != 0)
            noteOn (kUiNoteBase + i, i, 0.0f, 1.0f);
        if ((up & (1u << i)) != 0)
            noteOff (kUiNoteBase + i);
    }

    int cursor = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, numSamples, meta.samplePosition);
        renderVoices (out, cursor, at - cursor);
        cursor = at;
        handleMidi (meta.getMessage());
    }
    renderVoices (out, cursor, numSamples - cursor);

    const float master = juce::Decibels::decibelsToGain (globals.masterDb, -60.0f);
    out.applyGainRamp (0, numSamples, lastMaster, master);
    lastMaster = master;

    // posiciones para la interfaz: por pad, la voz mas nueva
    std::array<float, kNumPads> pos;
    std::array<juce::uint32, kNumPads> newest {};
    pos.fill (-1.0f);
    for (const auto& v : voices)
    {
        if (! v.active || v.src == nullptr || v.src->sourceLength <= 0)
            continue;
        if (v.age >= newest[(size_t) v.pad])
        {
            newest[(size_t) v.pad] = v.age;
            pos[(size_t) v.pad] = (float) (v.pos / v.src->posScale / (double) v.src->sourceLength);
        }
    }
    for (int i = 0; i < kNumPads; ++i)
        padPlayPos[(size_t) i].store (pos[(size_t) i], std::memory_order_relaxed);
}
}

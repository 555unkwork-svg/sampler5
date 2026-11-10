#include "SamplerEngine.h"

#include <cmath>

namespace eng
{
namespace
{
constexpr float kFastFadeMs = 5.0f;
constexpr float kEdgeFadeMs = 3.0f;
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
        uiVelocity[(size_t) i].store (1.0f);
    }
}

void SamplerEngine::prepare (double sampleRate)
{
    hostRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    numPending = 0;
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

void SamplerEngine::triggerPadFromUI (int pad, bool down, float velocity)
{
    if (pad < 0 || pad >= kNumPads)
        return;
    if (down)
        uiVelocity[(size_t) pad].store (juce::jlimit (0.0f, 1.0f, velocity));
    (down ? uiDown : uiUp).fetch_or (1u << pad);
}

void SamplerEngine::startRelease (Voice& v, float ms)
{
    const float samples = std::max (1.0f, ms * 0.001f * (float) hostRate);
    if (v.stage == Voice::Stage::release && v.envStep > 0.0f)
    {
        // ya esta soltando: solo aceleramos si el nuevo tiempo es mas corto
        v.envStep = std::max (v.envStep, v.env / samples);
        return;
    }
    v.stage = Voice::Stage::release;
    v.envStep = std::max (1.0e-6f, v.env / samples);
}

void SamplerEngine::updateVoiceRate (Voice& v) const
{
    const auto& p = pads[(size_t) v.pad];
    const double semis = (double) p.pitch + (double) p.fine * 0.01 + (double) v.keyOffset;
    v.rate = std::pow (2.0, semis / 12.0) * (v.src->audio->sampleRate / hostRate) * v.src->rateScale;
}

void SamplerEngine::noteOn (int noteId, int pad, float keyOffset, float velocity)
{
    if (current == nullptr || current->audio == nullptr || ! current->used[(size_t) pad])
        return;

    const auto& settings = pads[(size_t) pad];
    if (settings.mute)
        return;

    const int maxVoices = juce::jlimit (1, kMaxVoices, globals.mono ? 1 : globals.maxVoices);

    int sounding = 0;
    for (auto& v : voices)
    {
        if (! v.active)
            continue;
        // mono: cada nota corta todo. choke: corta a los otros pads. Siempre: una nota se corta a si misma
        if (globals.mono || v.noteId == noteId || (globals.chokeAll && v.pad != pad))
            startRelease (v, kFastFadeMs);
        if (v.stage != Voice::Stage::release)
            ++sounding;
    }

    // limite de voces: soltamos las mas viejas que sigan sonando
    while (sounding >= maxVoices)
    {
        Voice* oldest = nullptr;
        for (auto& v : voices)
            if (v.active && v.stage != Voice::Stage::release && (oldest == nullptr || v.age < oldest->age))
                oldest = &v;
        if (oldest == nullptr)
            break;
        startRelease (*oldest, kFastFadeMs);
        --sounding;
    }

    Voice* target = nullptr;
    for (auto& v : voices)
        if (! v.active) { target = &v; break; }

    if (target == nullptr)
    {
        // todas ocupadas (algunas apagandose): robamos la mas vieja
        target = &voices[0];
        for (auto& v : voices)
            if (v.age < target->age)
                target = &v;
    }

    auto& v = *target;
    v = Voice {};
    v.active = true;
    v.pad = pad;
    v.noteId = noteId;
    v.age = ++ageCounter;
    v.src = current;
    v.reverse = settings.reverse != globals.invertAll;
    v.gate = settings.gate;
    v.loop = settings.loop;
    v.keyOffset = keyOffset;
    v.startP = (double) current->start[(size_t) pad] * current->posScale;
    v.endP = (double) current->end[(size_t) pad] * current->posScale;
    v.pos = v.reverse ? v.endP - 1.0 : v.startP;

    v.velGain = 1.0f - globals.velocitySens * settings.velAmount * (1.0f - velocity);
    if (globals.humanize > 0.0f)
        v.velGain *= juce::Decibels::decibelsToGain ((random.nextFloat() * 2.0f - 1.0f) * 4.0f * globals.humanize);

    v.stage = Voice::Stage::attack;
    const float attackSamples = std::max (16.0f, settings.attackMs * 0.001f * (float) hostRate);
    v.envStep = 1.0f / attackSamples;
    updateVoiceRate (v);
}

void SamplerEngine::noteOff (int noteId)
{
    for (auto& v : voices)
        if (v.active && v.noteId == noteId && (v.gate || v.loop) && v.stage != Voice::Stage::release)
            startRelease (v, std::max (kFastFadeMs, pads[(size_t) v.pad].releaseMs));
}

void SamplerEngine::fire (const Event& e)
{
    if (e.on)
        noteOn (e.noteId, e.pad, e.keyOffset, e.velocity);
    else
        noteOff (e.noteId);
}

/** Cuantas muestras se retrasa una nota que llega en `time`: swing + humanizar. */
int SamplerEngine::delayFor (int time)
{
    double delay = 0.0;

    if (globals.swing > 0.505f && globals.hostPlaying && globals.hostBpm > 0.0)
    {
        // las semicorcheas que caen en contratiempo se corren hacia adelante
        const double ppq = globals.hostPpq + (double) time * globals.hostBpm / (60.0 * hostRate);
        const double phase = ppq * 2.0 - std::floor (ppq * 2.0);   // posicion dentro de la corchea
        if (phase >= 0.25 && phase < 0.75)
            delay += ((double) globals.swing - 0.5) * hostRate * 30.0 / globals.hostBpm;
    }

    if (globals.humanize > 0.0f)
        delay += (double) random.nextFloat() * (double) globals.humanize * 0.012 * hostRate;

    return (int) delay;
}

void SamplerEngine::addEvent (const Event& e, int numSamples)
{
    if (e.time < numSamples)
    {
        if (numBlockEvents >= kMaxEvents)
            return;
        // insercion ordenada y estable por tiempo
        int i = numBlockEvents++;
        while (i > 0 && blockEvents[(size_t) (i - 1)].time > e.time)
        {
            blockEvents[(size_t) i] = blockEvents[(size_t) (i - 1)];
            --i;
        }
        blockEvents[(size_t) i] = e;
    }
    else if (numPending < kMaxEvents)
    {
        auto later = e;
        later.time -= numSamples;
        pending[(size_t) numPending++] = later;
    }
}

void SamplerEngine::queueMidi (const juce::MidiMessage& m, int time, int numSamples)
{
    if (globals.midiChannel > 0 && m.getChannel() != globals.midiChannel)
        return;

    if (m.isChannelPressure() || m.isAftertouch())
    {
        const float pressure = (float) (m.isAftertouch() ? m.getAfterTouchValue() : m.getChannelPressureValue()) / 127.0f;
        for (auto& v : voices)
            if (v.active && (m.isChannelPressure() || v.noteId == m.getNoteNumber()))
                v.pressure = pressure;
        return;
    }

    if (m.isNoteOn())
    {
        const int note = m.getNoteNumber();
        if (! globals.midiIn || note < globals.rangeLo || note > globals.rangeHi)
            return;

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

        Event e;
        e.on = true;
        e.noteId = note;
        e.velocity = m.getFloatVelocity();
        e.pad = -1;

        if (globals.keysMode)
        {
            e.pad = juce::jlimit (0, kNumPads - 1, globals.selectedPad);
            e.keyOffset = (float) (note - globals.keysRoot);
        }
        else
        {
            for (int i = 0; i < kNumPads; ++i)
                if (padNote[(size_t) i].load() == note) { e.pad = i; break; }
            if (e.pad >= 0)
                lastHitPad.store (e.pad);   // como en una MPC: el pad tocado pasa a ser el pad en edicion
        }

        if (e.pad < 0)
            return;

        const int delay = delayFor (time);
        noteDelay[(size_t) note] = delay;
        e.time = time + delay;
        addEvent (e, numSamples);
    }
    else if (m.isNoteOff())
    {
        // el fin de la nota se retrasa lo mismo que su inicio
        Event e;
        e.noteId = m.getNoteNumber();
        e.time = time + noteDelay[(size_t) e.noteId];
        addEvent (e, numSamples);
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        panic.store (true);
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

        if (settings.mute && v.stage != Voice::Stage::release)
            startRelease (v, kFastFadeMs);

        updateVoiceRate (v);

        // paneo de potencia constante
        const float gain = juce::Decibels::decibelsToGain (settings.gainDb, -60.0f)
                         * juce::Decibels::decibelsToGain (settings.preGainDb) * v.velGain
                         * (globals.aftertouchVolume ? 1.0f + v.pressure : 1.0f);   // hasta +6 dB apretando
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

        // filtro de estado variable (TPT)
        const bool filterOn = settings.filter != FilterType::off;
        const float fc = juce::jlimit (20.0f, (float) hostRate * 0.45f, settings.cutoffHz);
        const float fg = std::tan (juce::MathConstants<float>::pi * fc / (float) hostRate);
        const float fk = 1.0f / (0.707f + 9.3f * juce::jlimit (0.0f, 1.0f, settings.resonance));
        const float fa1 = 1.0f / (1.0f + fg * (fg + fk));
        const float fa2 = fg * fa1;
        const float fa3 = fg * fa2;
        const bool highPass = settings.filter == FilterType::highPass;

        const double step = v.reverse ? -v.rate : v.rate;
        const double sliceLength = v.endP - v.startP;

        // fundido de borde: el corte se apaga justo antes de su final, sin pasarse al golpe siguiente
        const double edgeLen = std::max (1.0, std::min (v.rate * hostRate * (double) kEdgeFadeMs * 0.001,
                                                        sliceLength * 0.25));

        for (int i = 0; i < numSamples; ++i)
        {
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

            double remaining = v.reverse ? v.pos - v.startP : v.endP - v.pos;
            if (remaining <= 0.0 && v.loop && v.stage != Voice::Stage::release && sliceLength > 2.0)
            {
                // LOOP: vuelve al principio del corte
                v.pos = v.reverse ? v.endP - 1.0 : v.startP;
                remaining = sliceLength;
            }

            if (v.env <= 0.0f || remaining <= 0.0)
            {
                v.active = false;
                break;
            }

            double edge = remaining / edgeLen;
            if (v.loop)
                edge = std::min (edge, (sliceLength - remaining) / edgeLen + 0.02);   // tambien al re-entrar
            const float amp = v.env * (float) std::min (1.0, edge);

            float l = hermite (srcL, length, v.pos);
            float r = hermite (srcR, length, v.pos);

            if (filterOn)
            {
                float* in[2] = { &l, &r };
                for (int c = 0; c < 2; ++c)
                {
                    const float v3 = *in[c] - v.ic2[c];
                    const float v1 = fa1 * v.ic1[c] + fa2 * v3;
                    const float v2 = v.ic2[c] + fa2 * v.ic1[c] + fa3 * v3;
                    v.ic1[c] = 2.0f * v1 - v.ic1[c];
                    v.ic2[c] = 2.0f * v2 - v.ic2[c];
                    *in[c] = highPass ? *in[c] - fk * v1 - v2 : v2;
                }
            }

            v.gainL += stepL;
            v.gainR += stepR;

            if (outR != nullptr)
            {
                outL[i] += l * amp * v.gainL;
                outR[i] += r * amp * v.gainR;
            }
            else
            {
                outL[i] += 0.5f * amp * (l * v.gainL + r * v.gainR);
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
    {
        numPending = 0;
        for (auto& v : voices)
            if (v.active)
                startRelease (v, kFastFadeMs);
    }

    // eventos de este bloque: los que venian retrasados, la interfaz y el MIDI
    numBlockEvents = 0;
    const int waiting = numPending;
    numPending = 0;
    for (int i = 0; i < waiting; ++i)
        addEvent (pending[(size_t) i], numSamples);

    const auto down = uiDown.exchange (0);
    const auto up = uiUp.exchange (0);
    for (int i = 0; i < kNumPads; ++i)
    {
        if ((down & (1u << i)) != 0)
            addEvent ({ 0, true, kUiNoteBase + i, i, 0.0f, uiVelocity[(size_t) i].load() }, numSamples);
        if ((up & (1u << i)) != 0)
            addEvent ({ 0, false, kUiNoteBase + i, i, 0.0f, 1.0f }, numSamples);
    }

    for (const auto meta : midi)
        queueMidi (meta.getMessage(), juce::jlimit (0, std::max (0, numSamples - 1), meta.samplePosition), numSamples);

    int cursor = 0;
    for (int i = 0; i < numBlockEvents; ++i)
    {
        const auto& e = blockEvents[(size_t) i];
        const int at = juce::jlimit (cursor, numSamples, e.time);
        renderVoices (out, cursor, at - cursor);
        cursor = at;
        fire (e);
    }
    renderVoices (out, cursor, numSamples - cursor);

    // master y paneo global
    const float master = juce::Decibels::decibelsToGain (globals.masterDb, -60.0f);
    const float pan = juce::jlimit (-1.0f, 1.0f, globals.pan);
    const float panL = master * std::min (1.0f, 1.0f - pan);
    const float panR = master * std::min (1.0f, 1.0f + pan);
    if (out.getNumChannels() > 1)
    {
        out.applyGainRamp (0, 0, numSamples, lastPanL, panL);
        out.applyGainRamp (1, 0, numSamples, lastPanR, panR);
    }
    else
    {
        out.applyGainRamp (0, numSamples, lastMaster, master);
    }
    lastPanL = panL;
    lastPanR = panR;
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

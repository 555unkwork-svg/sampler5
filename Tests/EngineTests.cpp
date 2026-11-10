// Pruebas del motor con audio sintetico. Devuelve 0 si pasa todo.

#include "Engine/Analysis.h"
#include "Engine/SamplerEngine.h"
#include "Engine/Stretcher.h"

#include <cmath>
#include <cstdio>

static int failures = 0;

#define CHECK(cond, ...) \
    do { if (! (cond)) { ++failures; std::printf ("  FALLA: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (false)

static const double kRate = 44100.0;

/** Un loop de bateria de mentira: un golpe cada medio tiempo, con ruido que decae. */
static juce::AudioBuffer<float> makeDrumLoop (double bpm, int beats, std::vector<juce::int64>& hitPositions)
{
    const double beatLen = kRate * 60.0 / bpm;
    const int total = (int) (beatLen * beats);
    juce::AudioBuffer<float> buf (2, total);
    buf.clear();
    juce::Random rng (42);

    for (int h = 0; h < beats * 2; ++h)
    {
        const auto start = (juce::int64) std::llround (h * beatLen * 0.5);
        hitPositions.push_back (start);
        const bool strong = (h % 2) == 0;
        const double freq = strong ? 60.0 : 220.0;
        const int len = (int) (kRate * 0.12);
        for (int i = 0; i < len && start + i < total; ++i)
        {
            const float env = std::exp (-(float) i / (float) (kRate * 0.025));
            const float tone = (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / kRate);
            const float noise = rng.nextFloat() * 2.0f - 1.0f;
            // bombo fuerte en el tiempo, hi-hat suave en el contratiempo
            const float s = strong ? 0.8f * env * (0.8f * tone + 0.2f * noise)
                                   : 0.15f * env * noise;
            buf.addSample (0, (int) start + i, s);
            buf.addSample (1, (int) start + i, s);
        }
    }
    return buf;
}

static double dominantFrequency (const juce::AudioBuffer<float>& buf, int from, int to)
{
    // cruces por cero ascendentes
    const float* d = buf.getReadPointer (0);
    int crossings = 0, first = -1, last = -1;
    for (int i = from + 1; i < to; ++i)
        if (d[i - 1] <= 0.0f && d[i] > 0.0f)
        {
            if (first < 0) first = i;
            last = i;
            ++crossings;
        }
    if (crossings < 2) return 0.0;
    return (double) (crossings - 1) * kRate / (double) (last - first);
}

static void testAnalysis()
{
    std::printf ("Analisis (golpes y BPM)\n");
    for (double bpm : { 90.0, 120.0, 140.0, 174.0 })
    {
        std::vector<juce::int64> hits;
        const auto loop = makeDrumLoop (bpm, 16, hits);
        const auto a = eng::analyse (loop, kRate);

        int matched = 0;
        double worst = 0.0;
        for (auto hit : hits)
        {
            double best = 1.0e9;
            for (const auto& o : a.onsets)
                best = std::min (best, std::abs ((double) (o.position - hit)));
            if (best < kRate * 0.005) ++matched;
            worst = std::max (worst, std::min (best, 1.0e6));
        }
        std::printf ("  %.0f BPM: %d golpes detectados de %d (%d a menos de 5 ms), BPM estimado %.2f\n",
                     bpm, (int) a.onsets.size(), (int) hits.size(), matched, a.bpm);
        CHECK (matched >= (int) hits.size() - 1, "golpes sin detectar a %.0f BPM (peor error %.1f ms)", bpm, worst * 1000.0 / kRate);
        CHECK ((int) a.onsets.size() <= (int) hits.size() + 2, "golpes de mas a %.0f BPM", bpm);
        const bool tempoOk = std::abs (a.bpm - bpm) < 0.5 || std::abs (a.bpm * 2.0 - bpm) < 1.0 || std::abs (a.bpm - bpm * 2.0) < 1.0;
        CHECK (tempoOk, "BPM %.2f en vez de %.0f", a.bpm, bpm);
    }

    juce::AudioBuffer<float> silence (2, (int) kRate * 2);
    silence.clear();
    const auto s = eng::analyse (silence, kRate);
    CHECK (s.onsets.empty() && s.bpm == 0.0, "el silencio no deberia tener golpes");
}

/** Frases tipo "chop vocal": notas largas con un eco mas debil encima, como en un loop de voz. */
static void testVocalChops()
{
    std::printf ("Golpes en frases largas\n");
    const double bpm = 130.0;
    const int total = (int) std::llround (kRate * 60.0 / bpm * 32.0);   // 8 compases
    juce::AudioBuffer<float> buf (2, total);
    buf.clear();
    std::vector<juce::int64> mains;
    auto addNote = [&] (juce::int64 start, float amp, double freq, double decay)
    {
        for (int i = 0; i < (int) (kRate * 1.2) && start + i < total; ++i)
        {
            const float env = std::min (1.0f, (float) i / 200.0f) * std::exp (-(float) i / (float) (kRate * decay));
            float s = 0.0f;
            for (int h = 1; h <= 5; ++h)
                s += (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * h * i / kRate) / (float) h;
            buf.addSample (0, (int) start + i, amp * env * s * 0.4f);
            buf.addSample (1, (int) start + i, amp * env * s * 0.4f);
        }
    };
    for (int n = 0; n < 9; ++n)
    {
        const auto start = (juce::int64) (n * kRate * 60.0 / bpm * 4.0);   // una por compas
        if (start >= total) break;
        mains.push_back (start);
        const double freq = 180.0 + 30.0 * (n % 4);
        addNote (start, 0.9f, freq, 0.35);
        addNote (start + (juce::int64) (kRate * 0.33), 0.3f, freq, 0.25);   // el eco, con la nota aun sonando
    }

    const auto a = eng::analyse (buf, kRate);
    const auto slices = eng::makeSlices (eng::SliceMode::transients, a, total, kRate, bpm, 1.0, 0, 0.5);
    int found = 0;
    for (auto m : mains)
        for (auto s : slices)
            if (std::abs ((double) (s - m)) < kRate * 0.01) { ++found; break; }
    const auto more = eng::makeSlices (eng::SliceMode::transients, a, total, kRate, bpm, 1.0, 0, 1.0);
    const auto fewer = eng::makeSlices (eng::SliceMode::transients, a, total, kRate, bpm, 1.0, 0, 0.0);
    std::printf ("  %d frases: %d cortes (%d en su lugar); sensibilidad al maximo %d, al minimo %d; BPM %.2f\n",
                 (int) mains.size(), (int) slices.size(), found, (int) more.size(), (int) fewer.size(), a.bpm);
    CHECK (found == (int) mains.size(), "faltan frases principales");
    CHECK (slices.size() == mains.size(), "no deberia cortar en los ecos: %d cortes", (int) slices.size());
    CHECK (more.size() > slices.size(), "mas sensibilidad deberia dar mas cortes");
    CHECK (fewer.size() <= slices.size() && ! fewer.empty(), "menos sensibilidad no deberia dar mas cortes");
    CHECK (std::abs (a.bpm - 130.0) < 0.5, "BPM del loop: %.2f en vez de 130", a.bpm);

    CHECK (eng::bpmFromFileName ("91V_VHA3_130_vocal_vocoder_chop_wait_for_you_echo_A#m") == 130.0, "tempo del nombre de archivo");
    CHECK (eng::bpmFromFileName ("kick 01") == 0.0, "no deberia inventar un tempo");
}

static void testSlices()
{
    std::printf ("Cortes\n");
    std::vector<juce::int64> hits;
    const auto loop = makeDrumLoop (120.0, 16, hits);
    const auto a = eng::analyse (loop, kRate);
    const auto n = (juce::int64) loop.getNumSamples();

    for (auto mode : { eng::SliceMode::transients, eng::SliceMode::grid, eng::SliceMode::equal, eng::SliceMode::random })
    {
        const auto slices = eng::makeSlices (mode, a, n, kRate, 120.0, 1.0, 7);
        bool sorted = true, inRange = true;
        for (size_t i = 0; i < slices.size(); ++i)
        {
            if (i > 0 && slices[i] <= slices[i - 1]) sorted = false;
            if (slices[i] < 0 || slices[i] >= n) inRange = false;
        }
        CHECK (slices.size() == 16, "modo %d: %d cortes en vez de 16", (int) mode, (int) slices.size());
        CHECK (sorted && inRange, "modo %d: cortes desordenados o fuera de rango", (int) mode);
    }

    const auto grid = eng::makeSlices (eng::SliceMode::grid, a, n, kRate, 120.0, 1.0, 0);
    if (grid.size() >= 2)
        CHECK (std::abs ((double) (grid[1] - grid[0]) - kRate * 0.5) < 2.0, "la grilla de 1 tiempo a 120 BPM debe ser de 0.5 s");
    CHECK (eng::makeSlices (eng::SliceMode::manual, a, n, kRate, 120.0, 1.0, 0).empty(), "manual arranca vacio");
}

static void testStretch()
{
    std::printf ("Stretch\n");
    const int len = (int) kRate * 4;
    juce::AudioBuffer<float> sine (2, len);
    for (int i = 0; i < len; ++i)
    {
        const float s = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / kRate);
        sine.setSample (0, i, s);
        sine.setSample (1, i, s);
    }

    for (double speed : { 0.75, 1.25, 2.0 })
    {
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const auto out = eng::stretchOffline (sine, kRate, speed, 0.0);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const double freq = dominantFrequency (out, out.getNumSamples() / 4, out.getNumSamples() * 3 / 4);
        const float rms = out.getRMSLevel (0, out.getNumSamples() / 4, out.getNumSamples() / 2);
        std::printf ("  velocidad %.2f: largo %d (esperado %d), tono %.1f Hz, rms %.3f, %.0f ms de calculo\n",
                     speed, out.getNumSamples(), (int) std::llround (len / speed), freq, rms, ms);
        CHECK (std::abs (out.getNumSamples() - len / speed) < 2.0, "largo incorrecto");
        CHECK (std::abs (freq - 440.0) < 3.0, "el tono cambio: %.1f Hz", freq);
        CHECK (rms > 0.25f && rms < 0.45f, "nivel raro: %.3f", rms);
    }

    const auto up = eng::stretchOffline (sine, kRate, 1.0, 12.0);
    const double freq = dominantFrequency (up, up.getNumSamples() / 4, up.getNumSamples() * 3 / 4);
    std::printf ("  +12 semitonos: tono %.1f Hz, largo %d\n", freq, up.getNumSamples());
    CHECK (std::abs (freq - 880.0) < 6.0, "la transposicion fallo: %.1f Hz", freq);
    CHECK (up.getNumSamples() == len, "transponer no debe cambiar el largo");

    // los golpes tienen que quedar donde corresponde despues de estirar
    std::vector<juce::int64> hits;
    const auto loop = makeDrumLoop (120.0, 8, hits);
    const auto slow = eng::stretchOffline (loop, kRate, 0.8, 0.0);
    const auto a = eng::analyse (slow, kRate);
    int matched = 0;
    for (auto hit : hits)
        for (const auto& o : a.onsets)
            if (std::abs ((double) o.position - (double) hit / 0.8) < kRate * 0.012) { ++matched; break; }
    std::printf ("  golpes en su lugar tras estirar: %d de %d\n", matched, (int) hits.size());
    CHECK (matched >= (int) hits.size() - 2, "los golpes se corrieron al estirar");
}

static eng::PlayablePtr makePlayable (eng::AudioPtr audio, const std::vector<juce::int64>& slices)
{
    auto p = std::make_shared<eng::Playable>();
    p->audio = audio;
    p->sourceLength = audio->buffer.getNumSamples();
    for (size_t i = 0; i < slices.size() && i < (size_t) eng::kNumPads; ++i)
    {
        p->used[i] = true;
        p->start[i] = slices[i];
        p->end[i] = i + 1 < slices.size() ? slices[i + 1] : p->sourceLength;
    }
    return p;
}

static void testEngine()
{
    std::printf ("Motor de voces\n");
    auto audio = std::make_shared<eng::AudioData>();
    const int len = (int) kRate * 4;
    audio->sampleRate = kRate;
    audio->buffer.setSize (2, len);
    for (int i = 0; i < len; ++i)
    {
        // cada segundo tiene un tono distinto, asi sabemos que corte suena
        const double freq = 200.0 * (1 + i / (int) kRate);
        const float s = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / kRate);
        audio->buffer.setSample (0, i, s);
        audio->buffer.setSample (1, i, s);
    }

    eng::SamplerEngine engine;
    engine.prepare (kRate);
    CHECK (engine.padNote[0].load() == 60 && engine.padNote[15].load() == 75, "los pads deben ir desde la nota 60");
    for (int i = 0; i < 16; ++i)
        engine.padNote[(size_t) i].store (36 + i);   // el resto de la prueba usa notas fijas
    engine.setPlayable (makePlayable (audio, { 0, (juce::int64) kRate, (juce::int64) kRate * 2, (juce::int64) kRate * 3 }));

    const int block = 512;
    auto render = [&] (juce::MidiBuffer& midi, int blocks)
    {
        juce::AudioBuffer<float> out (2, block * blocks);
        for (int b = 0; b < blocks; ++b)
        {
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, b * block, block);
            engine.process (view, b == 0 ? midi : juce::MidiBuffer());
        }
        return out;
    };

    juce::MidiBuffer none;
    auto silent = render (none, 4);
    CHECK (silent.getMagnitude (0, silent.getNumSamples()) == 0.0f, "sin notas debe haber silencio");

    // pad 3 (nota 38) -> tercer segundo -> 600 Hz
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 38, 1.0f), 100);
    auto out = render (midi, 40);
    double freq = dominantFrequency (out, 4000, 16000);
    std::printf ("  pad 3: %.1f Hz (esperado 600), arranca en muestra 100: %s\n", freq,
                 out.getMagnitude (0, 0, 100) == 0.0f && out.getMagnitude (0, 100, 200) > 0.0f ? "si" : "no");
    CHECK (std::abs (freq - 600.0) < 2.0, "pad equivocado o tono incorrecto: %.1f", freq);
    CHECK (out.getMagnitude (0, 0, 100) == 0.0f, "la nota debe arrancar exactamente en su muestra");
    CHECK (engine.padPlayPos[2].load() > 0.5f, "falta la posicion de reproduccion del pad");

    // one-shot: termina al llegar al corte siguiente
    auto tail = render (none, 100);
    CHECK (tail.getMagnitude (0, tail.getNumSamples() - block, block) == 0.0f, "el corte debe terminar solo");
    bool clip = false;
    for (int i = 1; i < tail.getNumSamples(); ++i)
        if (std::abs (tail.getSample (0, i) - tail.getSample (0, i - 1)) > 0.2f) clip = true;
    CHECK (! clip, "click al terminar el corte");

    // el final del corte no debe dejar pasar nada del golpe siguiente
    {
        auto edge = std::make_shared<eng::AudioData>();
        edge->sampleRate = kRate;
        edge->buffer.setSize (2, (int) kRate * 2);
        edge->buffer.clear();
        for (int i = (int) kRate; i < (int) kRate * 2; ++i)
        {
            edge->buffer.setSample (0, i, 0.9f);
            edge->buffer.setSample (1, i, 0.9f);
        }
        engine.setPlayable (makePlayable (edge, { 0, (juce::int64) kRate }));
        juce::MidiBuffer me;
        me.addEvent (juce::MidiMessage::noteOn (1, 36, 1.0f), 0);
        auto o = render (me, 100);
        CHECK (o.getMagnitude (0, o.getNumSamples()) == 0.0f, "el corte dejo pasar el golpe siguiente: %.3f", o.getMagnitude (0, o.getNumSamples()));
        engine.setPlayable (makePlayable (audio, { 0, (juce::int64) kRate, (juce::int64) kRate * 2, (juce::int64) kRate * 3 }));
        render (none, 2);
    }

    // afinacion del pad: +12 -> el doble de frecuencia
    engine.pads[0].pitch = 12.0f;
    juce::MidiBuffer m2;
    m2.addEvent (juce::MidiMessage::noteOn (1, 36, 1.0f), 0);
    out = render (m2, 20);
    freq = dominantFrequency (out, 2000, 9000);
    CHECK (std::abs (freq - 400.0) < 2.0, "pitch +12 deberia dar 400 Hz, dio %.1f", freq);
    engine.pads[0].pitch = 0.0f;
    engine.allNotesOff();
    render (none, 4);

    // modo teclas: el pad elegido, cromatico
    engine.globals.keysMode = true;
    engine.globals.selectedPad = 1;   // 400 Hz
    engine.globals.keysRoot = 60;
    juce::MidiBuffer m3;
    m3.addEvent (juce::MidiMessage::noteOn (1, 67, 1.0f), 0);   // +7 semitonos
    out = render (m3, 20);
    freq = dominantFrequency (out, 2000, 9000);
    std::printf ("  modo teclas +7: %.1f Hz (esperado %.1f)\n", freq, 400.0 * std::pow (2.0, 7.0 / 12.0));
    CHECK (std::abs (freq - 400.0 * std::pow (2.0, 7.0 / 12.0)) < 3.0, "modo teclas desafinado");
    engine.globals.keysMode = false;
    engine.allNotesOff();
    render (none, 4);

    // reversa: el pad 1 al reves arranca desde el final de su corte
    engine.pads[0].reverse = true;
    juce::MidiBuffer m4;
    m4.addEvent (juce::MidiMessage::noteOn (1, 36, 1.0f), 0);
    out = render (m4, 10);
    CHECK (engine.padPlayPos[0].load() > 0.15f && engine.padPlayPos[0].load() < 0.25f, "reversa: posicion %.3f", engine.padPlayPos[0].load());
    engine.pads[0].reverse = false;
    engine.allNotesOff();
    render (none, 4);

    // gate: al soltar la nota se apaga
    engine.pads[0].gate = true;
    juce::MidiBuffer m5;
    m5.addEvent (juce::MidiMessage::noteOn (1, 36, 1.0f), 0);
    m5.addEvent (juce::MidiMessage::noteOff (1, 36), 300);
    out = render (m5, 8);
    CHECK (out.getMagnitude (0, 100, 150) > 0.1f, "gate: deberia sonar mientras se mantiene");
    CHECK (out.getMagnitude (0, 2048, 2048) == 0.0f, "gate: deberia apagarse al soltar");
    engine.pads[0].gate = false;

    // MIDI learn
    engine.learnPad.store (5);
    juce::MidiBuffer m6;
    m6.addEvent (juce::MidiMessage::noteOn (1, 36, 1.0f), 0);
    render (m6, 1);
    CHECK (engine.padNote[5].load() == 36 && engine.padNote[0].load() == -1 && engine.learnPad.load() == -1,
           "MIDI learn no asigno la nota");
    engine.allNotesOff();
    render (none, 4);

    // sync sin stretch: rateScale acelera la lectura
    auto fast = std::make_shared<eng::Playable> (*makePlayable (audio, { 0, (juce::int64) kRate }));
    fast->rateScale = 1.5;
    engine.padNote[0].store (36);
    engine.setPlayable (fast);
    juce::MidiBuffer m7;
    m7.addEvent (juce::MidiMessage::noteOn (1, 36, 1.0f), 0);
    out = render (m7, 20);
    freq = dominantFrequency (out, 2000, 9000);
    CHECK (std::abs (freq - 300.0) < 2.0, "rateScale 1.5 deberia dar 300 Hz, dio %.1f", freq);

    // ---- etapa 2 ----
    engine.setPlayable (makePlayable (audio, { 0, (juce::int64) kRate, (juce::int64) kRate * 2, (juce::int64) kRate * 3 }));
    for (int i = 0; i < 16; ++i)
        engine.padNote[(size_t) i].store (36 + i);
    engine.allNotesOff();
    render (none, 4);
    auto play = [&] (int note, int blocks)
    {
        juce::MidiBuffer mb;
        mb.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);
        return render (mb, blocks);
    };
    auto quiet = [&] { engine.allNotesOff(); render (none, 4); };

    // filtro: el pad 4 es un tono de 800 Hz; un pasa bajos a 100 Hz lo tiene que bajar mucho
    const float openLevel = play (39, 20).getRMSLevel (0, 4000, 4000);
    quiet();
    engine.pads[3].filter = eng::FilterType::lowPass;
    engine.pads[3].cutoffHz = 100.0f;
    const float lowLevel = play (39, 20).getRMSLevel (0, 4000, 4000);
    quiet();
    engine.pads[3].filter = eng::FilterType::highPass;
    engine.pads[3].cutoffHz = 8000.0f;
    const float highLevel = play (39, 20).getRMSLevel (0, 4000, 4000);
    quiet();
    engine.pads[3].filter = eng::FilterType::off;
    std::printf ("  filtro: abierto %.3f, pasa bajos a 100 Hz %.3f, pasa altos a 8 kHz %.3f\n", openLevel, lowLevel, highLevel);
    CHECK (lowLevel < openLevel * 0.1f && highLevel < openLevel * 0.1f, "el filtro no filtra");

    // ganancia y fino
    engine.pads[0].preGainDb = 6.0f;
    const float boosted = play (36, 10).getRMSLevel (0, 2000, 2000);
    quiet();
    engine.pads[0].preGainDb = 0.0f;
    const float normal = play (36, 10).getRMSLevel (0, 2000, 2000);
    quiet();
    CHECK (std::abs (boosted / normal - 1.995f) < 0.05f, "GANANCIA +6 dB dio x%.2f", boosted / normal);
    engine.pads[0].fine = 100.0f;
    out = play (36, 20);
    freq = dominantFrequency (out, 2000, 9000);
    CHECK (std::abs (freq - 200.0 * std::pow (2.0, 1.0 / 12.0)) < 1.5, "FINO +100 cents dio %.1f Hz", freq);
    engine.pads[0].fine = 0.0f;
    quiet();

    // loop: un corte de 0.1 s sigue sonando mientras se mantiene la nota, y sin clicks
    engine.setPlayable (makePlayable (audio, { 0, 4410 }));
    engine.pads[0].loop = true;
    out = play (36, 40);
    CHECK (out.getMagnitude (0, 15000, 4000) > 0.2f, "LOOP: deberia seguir sonando");
    bool loopClick = false;
    for (int i = 1; i < out.getNumSamples(); ++i)
        if (std::abs (out.getSample (0, i) - out.getSample (0, i - 1)) > 0.12f) loopClick = true;
    CHECK (! loopClick, "LOOP: click en la vuelta");
    juce::MidiBuffer offMsg;
    offMsg.addEvent (juce::MidiMessage::noteOff (1, 36), 0);
    out = render (offMsg, 10);
    CHECK (out.getMagnitude (0, 3000, 2000) == 0.0f, "LOOP: deberia cortar al soltar");
    engine.pads[0].loop = false;
    engine.setPlayable (makePlayable (audio, { 0, (juce::int64) kRate, (juce::int64) kRate * 2, (juce::int64) kRate * 3 }));
    render (none, 2);

    // silenciar un pad
    engine.pads[1].mute = true;
    CHECK (play (37, 6).getMagnitude (0, 0, 3000) == 0.0f, "pad silenciado no debe sonar");
    engine.pads[1].mute = false;

    // INV: todo al reves -> el pad 1 arranca desde el final de su corte
    engine.globals.invertAll = true;
    play (36, 4);
    CHECK (engine.padPlayPos[0].load() > 0.2f, "INV: posicion %.3f", engine.padPlayPos[0].load());
    engine.globals.invertAll = false;
    quiet();

    // voces maximas y choke
    auto countPlaying = [&]
    {
        int n = 0;
        for (int i = 0; i < 16; ++i) if (engine.padPlayPos[(size_t) i].load() >= 0.0f) ++n;
        return n;
    };
    engine.globals.maxVoices = 2;
    play (36, 2); play (37, 2); play (38, 2);
    render (none, 2);
    CHECK (countPlaying() == 2, "con 2 voces hay %d pads sonando", countPlaying());
    engine.globals.maxVoices = 16;
    quiet();
    engine.globals.chokeAll = true;
    play (36, 2); play (37, 2);
    render (none, 2);
    CHECK (countPlaying() == 1 && engine.padPlayPos[1].load() >= 0.0f, "choke: deberia quedar solo el ultimo pad");
    engine.globals.chokeAll = false;
    quiet();

    // paneo global a la derecha
    engine.globals.pan = 1.0f;
    out = play (36, 20);
    CHECK (out.getMagnitude (0, 5000, 4000) < 0.001f && out.getMagnitude (1, 5000, 4000) > 0.2f, "paneo global");
    engine.globals.pan = 0.0f;
    quiet();

    // swing: una nota en contratiempo se retrasa; una en el tiempo, no
    engine.globals.swing = 0.66f;
    engine.globals.hostPlaying = true;
    engine.globals.hostBpm = 120.0;
    engine.globals.hostPpq = 4.0;
    out = play (36, 4);
    CHECK (out.getMagnitude (0, 0, 64) > 0.0f, "swing: la nota en el tiempo no debe retrasarse");
    quiet();
    engine.globals.hostPpq = 4.25;
    out = play (36, 8);
    const int expectedDelay = (int) (0.16 * kRate * 30.0 / 120.0);
    int firstSound = -1;
    for (int i = 0; i < out.getNumSamples() && firstSound < 0; ++i)
        if (std::abs (out.getSample (0, i)) > 1.0e-5f) firstSound = i;
    std::printf ("  swing 66%%: la nota en contratiempo arranca en %d (esperado ~%d)\n", firstSound, expectedDelay);
    CHECK (std::abs (firstSound - expectedDelay) < 40, "swing: retraso %d en vez de %d", firstSound, expectedDelay);
    engine.globals.swing = 0.5f;
    engine.globals.hostPlaying = false;
    quiet();

    // humanizar: no rompe nada y varia el volumen
    engine.globals.humanize = 1.0f;
    float lo = 10.0f, hi = 0.0f;
    for (int n = 0; n < 12; ++n)
    {
        const float level = play (36, 12).getRMSLevel (0, 2500, 2000);
        lo = std::min (lo, level); hi = std::max (hi, level);
        quiet();
    }
    CHECK (hi > lo * 1.1f && lo > 0.1f, "humanizar: niveles %.3f a %.3f", lo, hi);
    engine.globals.humanize = 0.0f;

    // polifonia: muchas notas juntas no rompen nada
    juce::MidiBuffer many;
    engine.setPlayable (makePlayable (audio, { 0, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 11000, 12000, 13000, 14000, 15000 }));
    for (int i = 0; i < 16; ++i)
        engine.padNote[(size_t) i].store (36 + i);
    for (int rep = 0; rep < 4; ++rep)
        for (int i = 0; i < 16; ++i)
            many.addEvent (juce::MidiMessage::noteOn (1, 36 + i, 0.8f), rep * 100 + i);
    out = render (many, 20);
    bool finite = true;
    for (int i = 0; i < out.getNumSamples(); ++i)
        if (! std::isfinite (out.getSample (0, i))) finite = false;
    CHECK (finite, "salida no finita con muchas voces");

    engine.setPlayable (nullptr);
    render (none, 2);
    engine.purgeRetired();
}

int main()
{
    testAnalysis();
    testVocalChops();
    testSlices();
    testStretch();
    testEngine();
    std::printf (failures == 0 ? "\nTODO OK\n" : "\n%d FALLAS\n", failures);
    return failures == 0 ? 0 : 1;
}

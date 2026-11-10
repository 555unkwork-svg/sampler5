// Prueba de integracion: levanta el procesador real del plugin sin DAW,
// carga un audio, toca pads, prueba sync/stretch, guardado de estado y dibuja la interfaz.

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <cmath>
#include <cstdio>
#include <thread>

static int failures = 0;
#define CHECK(cond, ...) \
    do { if (! (cond)) { ++failures; std::printf ("  FALLA: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (false)

static const double kRate = 44100.0;

template <typename Fn>
static void onMessageThread (Fn&& fn)
{
    juce::WaitableEvent done;
    juce::MessageManager::callAsync ([&] { fn(); done.signal(); });
    done.wait();
}

struct FakePlayHead final : juce::AudioPlayHead
{
    std::atomic<double> bpm { 120.0 };
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm.load());
        info.setIsPlaying (true);
        return info;
    }
};

static juce::File writeDrumLoop (double bpm, int beats)
{
    const double beatLen = kRate * 60.0 / bpm;
    juce::AudioBuffer<float> buf (2, (int) (beatLen * beats));
    buf.clear();
    juce::Random rng (3);
    for (int h = 0; h < beats * 2; ++h)
    {
        const int start = (int) std::llround (h * beatLen * 0.5);
        const bool strong = h % 2 == 0;
        for (int i = 0; i < (int) (kRate * 0.12) && start + i < buf.getNumSamples(); ++i)
        {
            const float env = std::exp (-(float) i / (float) (kRate * 0.025));
            const float tone = (float) std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * i / kRate);
            const float noise = rng.nextFloat() * 2.0f - 1.0f;
            const float s = strong ? 0.8f * env * (0.8f * tone + 0.2f * noise) : 0.15f * env * noise;
            buf.setSample (0, start + i, s);
            buf.setSample (1, start + i, s);
        }
    }

    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sampler5_test_loop.wav");
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (kRate).withNumChannels (2).withBitsPerSample (24));
    writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    writer.reset();
    return file;
}

static bool waitUntilIdle (SamplerProcessor& proc, int extraMs = 400)
{
    juce::Thread::sleep (extraMs);
    for (int i = 0; i < 600; ++i)
    {
        bool busy = true;
        onMessageThread ([&] { busy = proc.isBusy(); });
        if (! busy) return true;
        juce::Thread::sleep (50);
    }
    return false;
}

/** Toca una nota y devuelve cuantas muestras suena. */
static int playAndMeasure (SamplerProcessor& proc, int note, int blocks, float* peakOut = nullptr)
{
    const int block = 512;
    int lastLoud = 0;
    float peak = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);
        proc.processBlock (buffer, midi);
        for (int i = 0; i < block; ++i)
        {
            const float v = std::abs (buffer.getSample (0, i));
            peak = std::max (peak, v);
            if (v > 1.0e-4f) lastLoud = b * block + i;
        }
    }
    if (peakOut != nullptr) *peakOut = peak;
    return lastLoud;
}

static void runTests (const juce::String& screenshotPath)
{
    std::unique_ptr<SamplerProcessor> proc;
    FakePlayHead playHead;
    const auto file = writeDrumLoop (120.0, 8);

    onMessageThread ([&]
    {
        proc = std::make_unique<SamplerProcessor>();
        proc->setPlayHead (&playHead);
        proc->setRateAndBufferSizeDetails (kRate, 512);
        proc->prepareToPlay (kRate, 512);
        proc->loadFile (file);
    });

    std::printf ("Carga\n");
    CHECK (waitUntilIdle (*proc), "la carga no termino");
    int used = 0;
    double bpm = 0.0;
    onMessageThread ([&]
    {
        for (const auto& s : proc->getSlices()) used += s.used ? 1 : 0;
        bpm = proc->getSampleBpm();
        std::printf ("  %s, BPM %.2f, %d cortes, estado: %s\n", proc->getFileName().toRawUTF8(), bpm, used, proc->getStatusText().toRawUTF8());
    });
    CHECK (used == 16, "esperaba 16 cortes, hay %d", used);
    CHECK (std::abs (bpm - 120.0) < 0.5, "BPM %.2f", bpm);

    std::printf ("Pads y sync\n");
    onMessageThread ([&] { proc->autoSlice (eng::SliceMode::grid, 2); });   // 1 tiempo = 0.5 s
    juce::Thread::sleep (100);
    float peak = 0.0f;
    const int plain = playAndMeasure (*proc, 60, 80, &peak);
    std::printf ("  pad 1 sin sync: suena %d muestras (corte de 22050), pico %.2f\n", plain, peak);
    CHECK (peak > 0.3f, "el pad no suena");
    CHECK (plain > 21500 && plain <= 22050, "duracion sin sync: %d (no debe pasarse del corte)", plain);

    // proyecto a 150 BPM con sync + stretch: el corte dura 22050 / 1.25
    playHead.bpm = 150.0;
    onMessageThread ([&] { proc->apvts.getParameter ("sync")->setValueNotifyingHost (1.0f); });
    playAndMeasure (*proc, 0, 4);   // el procesador lee el tempo en processBlock
    CHECK (waitUntilIdle (*proc, 800), "el stretch no termino");
    juce::Thread::sleep (200);
    const int synced = playAndMeasure (*proc, 60, 80);
    std::printf ("  pad 1 con sync a 150 BPM (stretch): %d muestras (esperado ~17640)\n", synced);
    CHECK (synced > 17640 - 400 && synced <= 17640 + 2, "duracion con sync+stretch: %d", synced);

    // sync sin stretch: misma duracion, por remuestreo
    onMessageThread ([&] { proc->apvts.getParameter ("stretch")->setValueNotifyingHost (0.0f); });
    juce::Thread::sleep (300);
    const int resampled = playAndMeasure (*proc, 60, 80);
    std::printf ("  pad 1 con sync a 150 BPM (sin stretch): %d muestras\n", resampled);
    CHECK (resampled > 17640 - 400 && resampled <= 17640 + 2, "duracion con sync sin stretch: %d", resampled);

    std::printf ("Cortes: fin, nombres, favoritos, zona e iman\n");
    onMessageThread ([&]
    {
        proc->apvts.getParameter ("sync")->setValueNotifyingHost (0.0f);
        proc->autoSlice (eng::SliceMode::grid, 2);
        const auto start0 = proc->getSlices()[0].start;
        CHECK (proc->getSliceEnd (0) == proc->getSlices()[1].start, "fin automatico = inicio del siguiente");
        proc->setSliceEnd (0, start0 + 4410);
        CHECK (proc->getSliceEnd (0) == start0 + 4410, "fin propio");
    });
    juce::Thread::sleep (150);
    const int shortened = playAndMeasure (*proc, 60, 40);
    std::printf ("  pad 1 con FINAL a 100 ms: suena %d muestras\n", shortened);
    CHECK (shortened > 4000 && shortened <= 4410, "el fin propio no se respeta: %d", shortened);
    onMessageThread ([&]
    {
        proc->setSliceEnd (0, -1);
        proc->setSliceName (2, "  Bombo  ");
        proc->setSliceFavorite (2, true);
        const auto favStart = proc->getSlices()[2].start;
        proc->autoSlice (eng::SliceMode::equal, 2);
        CHECK (proc->getSliceName (2) == "Bombo" && proc->getSlices()[2].favorite && proc->getSlices()[2].start == favStart,
               "el favorito no sobrevivio al regenerar");
        CHECK (proc->getSliceName (1) == "CHOP 2", "nombre por defecto: %s", proc->getSliceName (1).toRawUTF8());
        proc->setSliceFavorite (2, false);

        // zona de fuente: solo se corta adentro
        const auto len = (juce::int64) proc->getSource()->buffer.getNumSamples();
        proc->setRegion (len / 4, len / 2);
        proc->autoSlice (eng::SliceMode::equal, 2);
        bool inside = true;
        for (int i = 0; i < eng::kNumPads; ++i)
            if (proc->getSlices()[(size_t) i].used
                && (proc->getSlices()[(size_t) i].start < len / 4 || proc->getSliceEnd (i) > len / 2))
                inside = false;
        CHECK (inside, "hay cortes fuera de la zona de fuente");
        proc->setRegion (0, len);

        // iman: cerca de un golpe se pega; lejos, al cruce por cero o no se mueve
        const auto onset = proc->getAnalysis().onsets[2].position;
        proc->setSnap (true, SamplerProcessor::SnapMode::onset);
        CHECK (proc->snapPosition (onset + 300, 500) == onset, "el iman no llevo al golpe");
        CHECK (proc->snapPosition (onset + 3000, 500) == onset + 3000, "el iman actuo demasiado lejos");
        proc->setSnap (false, SamplerProcessor::SnapMode::onset);
        CHECK (proc->snapPosition (onset + 300, 500) == onset + 300, "iman apagado no debe mover");
        proc->setSnap (true, SamplerProcessor::SnapMode::automatic);
        proc->setSliceName (5, "Caja");
        proc->setSliceFavorite (5, true);
        proc->setSliceEnd (5, proc->getSlices()[5].start + 2000);
        proc->apvts.getParameter ("sync")->setValueNotifyingHost (1.0f);
    });

    std::printf ("Estado guardado\n");
    juce::MemoryBlock state;
    std::array<SamplerProcessor::Slice, eng::kNumPads> before {};
    onMessageThread ([&]
    {
        proc->setSliceStart (3, 12345);
        proc->engine.padNote[3].store (100);
        proc->setSampleBpm (121.5);
        before = proc->getSlices();
        proc->getStateInformation (state);
    });

    std::unique_ptr<SamplerProcessor> second;
    onMessageThread ([&]
    {
        second = std::make_unique<SamplerProcessor>();
        second->setRateAndBufferSizeDetails (kRate, 512);
        second->prepareToPlay (kRate, 512);
        second->setStateInformation (state.getData(), (int) state.getSize());
    });
    CHECK (waitUntilIdle (*second), "la restauracion no termino");
    onMessageThread ([&]
    {
        bool same = true;
        for (int i = 0; i < eng::kNumPads; ++i)
            if (second->getSlices()[(size_t) i].used != before[(size_t) i].used || second->getSlices()[(size_t) i].start != before[(size_t) i].start)
                same = false;
        std::printf ("  archivo %s, BPM %.2f, cortes iguales: %s, nota pad 4: %d, sync: %.0f\n", second->getFileName().toRawUTF8(),
                     second->getSampleBpm(), same ? "si" : "no", second->engine.padNote[3].load(),
                     (double) second->apvts.getRawParameterValue ("sync")->load());
        CHECK (same, "los cortes no se restauraron");
        CHECK (second->getSliceName (5) == "Caja" && second->getSlices()[5].favorite
                   && second->getSliceEnd (5) == second->getSlices()[5].start + 2000,
               "nombre, favorito o fin del corte no se restauraron");
        CHECK (std::abs (second->getSampleBpm() - 121.5) < 0.001, "el BPM no se restauro");
        CHECK (second->engine.padNote[3].load() == 100, "la nota aprendida no se restauro");
        CHECK (second->getSource() != nullptr, "el audio no se volvio a cargar");
        CHECK (second->apvts.getRawParameterValue ("sync")->load() > 0.5f, "los parametros no se restauraron");
    });
    float peak2 = 0.0f;
    playAndMeasure (*second, 100, 20, &peak2);
    CHECK (peak2 > 0.01f, "el plugin restaurado no suena");

    std::printf ("Audio dentro del proyecto y presets\n");
    {
        // el audio viaja en el estado: se restaura aunque el archivo ya no este
        juce::MemoryBlock withAudio;
        bool embedded = false;
        onMessageThread ([&] { embedded = second->hasEmbeddedAudio(); second->getStateInformation (withAudio); });
        CHECK (embedded, "el audio no quedo guardado para el proyecto");
        const auto hidden = file.getSiblingFile ("sampler5_test_loop_oculto.wav");
        hidden.deleteFile();
        file.moveFileTo (hidden);

        std::unique_ptr<SamplerProcessor> third;
        onMessageThread ([&]
        {
            third = std::make_unique<SamplerProcessor>();
            third->setRateAndBufferSizeDetails (kRate, 512);
            third->prepareToPlay (kRate, 512);
            third->setStateInformation (withAudio.getData(), (int) withAudio.getSize());
        });
        CHECK (waitUntilIdle (*third), "la restauracion no termino");
        bool hasAudio = false;
        onMessageThread ([&] { hasAudio = third->getSource() != nullptr && third->getSliceName (5) == "Caja"; });
        std::printf ("  estado de %d KB; sin el archivo en disco, el audio vuelve: %s\n", (int) (withAudio.getSize() / 1024), hasAudio ? "si" : "no");
        CHECK (hasAudio, "el audio guardado en el proyecto no se restauro");
        float p3 = 0.0f;
        playAndMeasure (*third, 100, 20, &p3);
        CHECK (p3 > 0.01f, "el plugin restaurado desde el proyecto no suena");
        hidden.moveFileTo (file);

        // presets: guardar, volver al predeterminado y cargar
        const juce::String presetName ("prueba_automatica_sampler5");
        bool saved = false, listed = false;
        onMessageThread ([&]
        {
            saved = third->savePreset (presetName);
            listed = third->getPresetNames().contains (presetName);
            third->loadDefaultPreset();
        });
        CHECK (saved && listed, "el preset no se guardo");
        CHECK (waitUntilIdle (*third), "predeterminado");
        bool empty = false;
        onMessageThread ([&] { empty = third->getSource() == nullptr && third->apvts.getRawParameterValue ("sync")->load() < 0.5f; });
        CHECK (empty, "Predeterminado deberia dejar el plugin vacio");
        bool loadedOk = false;
        onMessageThread ([&] { loadedOk = third->loadPreset (presetName); });
        CHECK (loadedOk && waitUntilIdle (*third), "el preset no cargo");
        bool back = false;
        onMessageThread ([&]
        {
            back = third->getSource() != nullptr && third->getSliceName (5) == "Caja" && third->getPresetName() == presetName;
            SamplerProcessor::getPresetFolder().getChildFile (presetName + ".s5kit").deleteFile();
            third.reset();
        });
        std::printf ("  preset guardado, vaciado y vuelto a cargar: %s\n", back ? "si" : "no");
        CHECK (back, "el preset no devolvio el kit");
    }

    // sin tempo del host (standalone): SYNC usa el tempo manual
    {
        std::unique_ptr<SamplerProcessor> solo;
        onMessageThread ([&]
        {
            solo = std::make_unique<SamplerProcessor>();
            solo->setRateAndBufferSizeDetails (kRate, 512);
            solo->prepareToPlay (kRate, 512);
            solo->loadFile (file);
        });
        CHECK (waitUntilIdle (*solo), "la carga no termino");
        onMessageThread ([&]
        {
            solo->autoSlice (eng::SliceMode::grid, 2);
            solo->apvts.getParameter ("sync")->setValueNotifyingHost (1.0f);
            solo->setManualBpm (150.0);
        });
        CHECK (waitUntilIdle (*solo, 800), "el stretch no termino");
        juce::Thread::sleep (200);
        const int manual = playAndMeasure (*solo, 60, 80);
        std::printf ("  sync con tempo manual 150 (sin host): %d muestras (esperado ~17640)\n", manual);
        CHECK (manual > 17640 - 400 && manual <= 17640 + 2, "sync con tempo manual: %d", manual);
        onMessageThread ([&] { solo.reset(); });
    }

    std::printf ("Interfaz\n");
    onMessageThread ([&]
    {
        second->setSelectedPad (3);
        second->engine.triggerPadFromUI (5, true);
    });
    playAndMeasure (*second, 0, 6);
    onMessageThread ([&]
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (second->createEditor());
        CHECK (editor != nullptr, "no hay editor");
        if (editor != nullptr && screenshotPath.isNotEmpty())
        {
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
            juce::File out (screenshotPath);
            out.deleteFile();
            if (auto stream = out.createOutputStream())
            {
                juce::PNGImageFormat png;
                png.writeImageToStream (image, *stream);
                std::printf ("  captura guardada: %dx%d\n", image.getWidth(), image.getHeight());
            }

            // las otras vistas y pestanas, para revisarlas a ojo
            if (auto* ed = dynamic_cast<SamplerEditor*> (editor.get()))
            {
                auto shot = [&] (const char* suffix)
                {
                    juce::File f (screenshotPath.upToLastOccurrenceOf (".", false, false) + suffix + ".png");
                    f.deleteFile();
                    if (auto stream = f.createOutputStream())
                        juce::PNGImageFormat().writeImageToStream (ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f), *stream);
                };
                ed->setViewMode (WaveformView::Mode::source);   ed->setPanelTab (SamplerEditor::tabGlobal); shot ("-fuente-global");
                ed->setViewMode (WaveformView::Mode::analysis); ed->setPanelTab (SamplerEditor::tabMidi);   shot ("-analisis-midi");
                ed->setViewMode (WaveformView::Mode::slices);   ed->setPanelTab (SamplerEditor::tabFilter);   shot ("-filtro");
                ed->setPanelTab (SamplerEditor::tabAdvanced); shot ("-avanzado");
            }
        }
    });

    onMessageThread ([&] { proc.reset(); second.reset(); });
    file.deleteFile();
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String screenshot = argc > 1 ? juce::String (argv[1]) : juce::String();

    std::thread worker ([&]
    {
        runTests (screenshot);
        juce::MessageManager::getInstance()->stopDispatchLoop();
    });
    juce::MessageManager::getInstance()->runDispatchLoop();
    worker.join();

    std::printf (failures == 0 ? "\nTODO OK\n" : "\n%d FALLAS\n", failures);
    return failures == 0 ? 0 : 1;
}

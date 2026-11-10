// Carga el VST3 ya compilado como lo haria un DAW y comprueba que los parametros
// cambian el sonido pasando por el envoltorio VST3 real.
// Uso: HostTests <ruta al .vst3> <estado guardado por PluginTests>

#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdio>
#include <thread>

static int failures = 0;
#define CHECK(cond, ...) \
    do { if (! (cond)) { ++failures; std::printf ("  FALLA: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (false)

template <typename Fn>
static void onMessageThread (Fn&& fn)
{
    juce::WaitableEvent done;
    juce::MessageManager::callAsync ([&] { fn(); done.signal(); });
    done.wait();
}

static std::unique_ptr<juce::AudioPluginInstance> plugin;

static juce::AudioProcessorParameter* find (const juce::String& name)
{
    for (auto* p : plugin->getParameters())
        if (p->getName (64) == name)
            return p;
    return nullptr;
}

static void set (const juce::String& name, float normalised)
{
    auto* p = find (name);
    CHECK (p != nullptr, "el host no ve el parametro \"%s\"", name.toRawUTF8());
    if (p != nullptr)
        p->setValue (normalised);
}

/** Toca una nota y devuelve el pico (canal 0 o 1). */
static float play (int note, int channel = 0, int midiChannel = 1)
{
    float peak = 0.0f;
    for (int b = 0; b < 60; ++b)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        buffer.clear();
        juce::MidiBuffer midi;
        if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (midiChannel, note, 1.0f), 0);
        if (b == 3) midi.addEvent (juce::MidiMessage::noteOff (midiChannel, note), 0);
        plugin->processBlock (buffer, midi);
        peak = std::max (peak, buffer.getMagnitude (channel, 0, 512));
    }
    return peak;
}

static void run (const juce::String& path, const juce::File& stateFile)
{
    onMessageThread ([&]
    {
        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> found;
        format.findAllTypesForFile (found, path);
        CHECK (found.size() > 0, "no se encontro el plugin en %s", path.toRawUTF8());
        if (found.size() == 0)
            return;
        juce::String error;
        plugin = format.createInstanceFromDescription (*found[0], 44100.0, 512, error);
        CHECK (plugin != nullptr, "no cargo: %s", error.toRawUTF8());
        if (plugin == nullptr)
            return;
        plugin->enableAllBuses();
        plugin->prepareToPlay (44100.0, 512);
        // el host guarda el estado del plugin dentro de su propio sobre: lo armamos igual
        juce::MemoryBlock raw;
        stateFile.loadFileAsData (raw);
        juce::XmlElement envelope ("VST3PluginState");
        envelope.createNewChildElement ("IComponent")->addTextElement (raw.toBase64Encoding());
        juce::MemoryBlock state;
        juce::AudioProcessor::copyXmlToBinary (envelope, state);
        plugin->setStateInformation (state.getData(), (int) state.getSize());
        std::printf ("  cargado %s, %d parametros a la vista del host\n", found[0]->name.toRawUTF8(), plugin->getParameters().size());
    });
    if (plugin == nullptr)
        return;

    // el plugin restaura el audio en segundo plano
    float base = 0.0f;
    for (int tries = 0; tries < 100 && base < 0.05f; ++tries)
    {
        juce::Thread::sleep (100);
        base = play (60);
    }
    std::printf ("  nivel base del pad 1: %.2f\n", base);
    CHECK (base > 0.2f, "el plugin no suena al restaurar el proyecto");

    set ("Pad 1 Silenciar", 1.0f);  CHECK (play (60) == 0.0f, "SILENCIAR");             set ("Pad 1 Silenciar", 0.0f);
    set ("Pad 1 Ganancia", 0.25f);  CHECK (play (60) > base * 1.8f, "GANANCIA");         set ("Pad 1 Ganancia", 0.0f);
    set ("Pad 1 Filtro", 0.5f); set ("Pad 1 Corte", 0.0f);
    const float filtered = play (60);
    CHECK (filtered < base * 0.5f, "FILTRO: %.2f de %.2f", filtered, base);
    set ("Pad 1 Filtro", 0.0f); set ("Pad 1 Corte", 1.0f);
    set ("Pan", 1.0f); play (60);
    CHECK (play (60, 0) < 0.001f && play (60, 1) > 0.2f, "PAN global");
    set ("Pan", 0.5f); play (60);
    set ("Entrada MIDI", 0.0f);     CHECK (play (60) == 0.0f, "ENTRADA MIDI");           set ("Entrada MIDI", 1.0f);
    set ("Canal MIDI", 5.0f / 16.0f);
    CHECK (play (60, 0, 1) == 0.0f && play (60, 0, 5) > 0.2f, "CANAL MIDI");
    set ("Canal MIDI", 0.0f);
    set ("Master", 0.0f); play (60); CHECK (play (60) < 0.01f, "MASTER");                 set ("Master", 60.0f / 66.0f); play (60);
    std::printf ("  silenciar, ganancia, filtro, pan, entrada y canal MIDI: probados a traves del VST3\n");

    onMessageThread ([] { plugin.reset(); });
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3)
        return 2;
    std::printf ("Plugin VST3 cargado como en un DAW\n");
    std::thread worker ([&]
    {
        run (argv[1], juce::File (argv[2]));
        juce::MessageManager::getInstance()->stopDispatchLoop();
    });
    juce::MessageManager::getInstance()->runDispatchLoop();
    worker.join();
    std::printf (failures == 0 ? "\nTODO OK\n" : "\n%d FALLAS\n", failures);
    return failures == 0 ? 0 : 1;
}

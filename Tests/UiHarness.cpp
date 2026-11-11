// Arnes para probar la interfaz de verdad: abre el editor en pantalla con un audio cargado y
// vuelca el estado a un archivo, para manejarlo con clics reales (xdotool) y ver que cambia.

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <thread>

struct Dumper final : juce::Timer
{
    SamplerProcessor& proc;
    juce::File out;
    Dumper (SamplerProcessor& p, juce::File f) : proc (p), out (std::move (f)) { startTimer (150); }

    void timerCallback() override
    {
        juce::String text;
        for (auto* p : proc.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (std::abs (r->getValue() - r->getDefaultValue()) > 1.0e-4f)
                    text << r->getParameterID() << "=" << r->getCurrentValueAsText() << "\n";
        text << "selected=" << proc.getSelectedPad() + 1 << "\n";
        text << "snap=" << (int) proc.isSnapEnabled() << "/" << (int) proc.getSnapMode() << "\n";
        text << "mode=" << (int) proc.getSliceMode() << " key=" << proc.getKeyText() << " preset=" << proc.getPresetName() << "\n";
        text << "region=" << proc.getRegionStart() << ".." << proc.getRegionEnd() << "\n";
        int used = 0;
        for (int i = 0; i < eng::kNumPads; ++i)
        {
            const auto& s = proc.getSlices()[(size_t) i];
            if (! s.used) continue;
            ++used;
            if (i < 4)
                text << "pad" << i + 1 << ": " << s.start << ".." << proc.getSliceEnd (i) << " " << proc.getSliceName (i)
                     << (s.favorite ? " FAV" : "") << "\n";
        }
        text << "used=" << used << "\n";
        out.replaceWithText (text);
    }
};

struct Holder final : juce::Component, juce::ComponentListener
{
    juce::AudioProcessorEditor& editor;
    explicit Holder (juce::AudioProcessorEditor& e) : editor (e)
    {
        setOpaque (true);
        addAndMakeVisible (editor);
        editor.addComponentListener (this);
        fit();
        addToDesktop (0);
        setTopLeftPosition (0, 0);
        setVisible (true);
    }
    ~Holder() override { editor.removeComponentListener (this); }
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }
    void componentMovedOrResized (juce::Component&, bool, bool) override { fit(); }
    void fit()
    {
        const auto area = getLocalArea (&editor, editor.getLocalBounds());
        setSize (area.getWidth(), area.getHeight());
    }
};

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    auto proc = std::make_unique<SamplerProcessor>();
    proc->setRateAndBufferSizeDetails (44100.0, 512);
    proc->prepareToPlay (44100.0, 512);
    if (argc > 1)
        proc->loadFile (juce::File (argv[1]));

    // el editor va dentro de un contenedor, como hacen los DAW: la ventana sigue su tamano
    std::unique_ptr<juce::AudioProcessorEditor> editor (proc->createEditor());
    Holder holder (*editor);

    Dumper dumper (*proc, juce::File (argc > 2 ? argv[2] : "/tmp/ui_state.txt"));

    std::atomic<bool> running { true };
    std::thread audio ([&]
    {
        juce::AudioBuffer<float> buffer (2, 512);
        while (running)
        {
            juce::MidiBuffer midi;
            proc->processBlock (buffer, midi);
            juce::Thread::sleep (10);
        }
    });

    juce::MessageManager::getInstance()->runDispatchLoop();
    running = false;
    audio.join();
    editor.reset();
    return 0;
}

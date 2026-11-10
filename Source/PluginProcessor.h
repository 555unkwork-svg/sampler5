#pragma once

#include "Engine/Analysis.h"
#include "Engine/SamplerEngine.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <optional>

/** Procesador del plugin: parametros, carga de audio, cortes, sync/stretch y estado guardado.
    Todo lo "de modelo" (cortes, BPM, archivo) se toca solo desde el hilo de mensajes. */
class SamplerProcessor final : public juce::AudioProcessor,
                               public juce::ChangeBroadcaster,
                               private juce::Timer
{
public:
    static constexpr double kMaxSeconds = 300.0;   // largo maximo del audio que se carga

    struct Slice
    {
        bool used = false;
        juce::int64 start = 0;
    };

    SamplerProcessor();
    ~SamplerProcessor() override;

    //---- AudioProcessor ----
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //---- modelo (hilo de mensajes) ----
    void loadFile (const juce::File& file);
    bool isBusy() const { return loading || stretchRunning; }
    juce::String getStatusText() const;
    juce::String getFileName() const { return sampleFile.getFileName(); }

    eng::AudioPtr getSource() const { return source; }
    const std::array<Slice, eng::kNumPads>& getSlices() const { return slices; }

    double getSampleBpm() const { return sampleBpm; }
    void setSampleBpm (double bpm);
    double getHostBpm() const { return hostBpm.load(); }

    eng::SliceMode getSliceMode() const { return sliceMode; }
    int getGridIndex() const { return gridIndex; }
    void autoSlice (eng::SliceMode mode, int gridIndexToUse);
    void setSliceStart (int pad, juce::int64 position);
    int addSlice (juce::int64 position);   // devuelve el pad usado, o -1 si no queda ninguno libre
    void removeSlice (int pad);

    int getSelectedPad() const { return selectedPad.load(); }
    void setSelectedPad (int pad);

    static juce::String padParamId (int pad, const char* name);
    static const juce::StringArray& gridNames();
    static double gridBeatsForIndex (int index);

    juce::AudioProcessorValueTreeState apvts;
    eng::SamplerEngine engine;

private:
    struct StretchKey
    {
        double speed = 1.0, tune = 0.0;
        bool operator== (const StretchKey& o) const { return std::abs (speed - o.speed) < 1.0e-4 && std::abs (tune - o.tune) < 1.0e-3; }
    };

    struct RestoreInfo
    {
        juce::File file;
        double bpm = 0.0;
        std::array<Slice, eng::kNumPads> slices {};
    };

    struct LoadResult
    {
        int generation = 0;
        juce::File file;
        eng::AudioPtr audio;
        eng::Analysis analysis;
        juce::String error;
        std::optional<RestoreInfo> restore;
    };

    struct StretchResult
    {
        int generation = 0;
        StretchKey key;
        eng::AudioPtr audio;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void startLoad (const juce::File& file, std::optional<RestoreInfo> restore);
    void finishLoad (LoadResult&);
    void rebuildPlayable();
    void updateTempoTarget();
    double currentSpeed() const;

    // parametros (punteros crudos, validos mientras viva apvts)
    struct PadParamPtrs { std::atomic<float>* gain, *pan, *pitch, *attack, *release, *reverse, *gate; };
    std::array<PadParamPtrs, eng::kNumPads> padParams {};
    std::atomic<float>* masterParam = nullptr, *velParam = nullptr, *monoParam = nullptr, *syncParam = nullptr,
                      * stretchParam = nullptr, *tuneParam = nullptr, *keysParam = nullptr, *rootParam = nullptr;

    // modelo
    juce::CriticalSection modelLock;      // para get/setStateInformation desde otros hilos
    juce::File sampleFile;
    eng::AudioPtr source;
    eng::Analysis analysis;
    double sampleBpm = 120.0;
    std::array<Slice, eng::kNumPads> slices {};
    eng::SliceMode sliceMode = eng::SliceMode::transients;
    int gridIndex = 2;
    std::atomic<int> selectedPad { 0 };
    juce::String errorText;

    // stretch
    eng::AudioPtr stretched;
    StretchKey stretchedKey;
    StretchKey appliedKey;
    bool appliedStretchOn = false;
    StretchKey requestedKey;
    bool stretchRunning = false;
    int stableTicks = 0;
    StretchKey lastSeenKey;

    // trabajos en segundo plano
    juce::AudioFormatManager formats;
    juce::ThreadPool pool { 1 };
    juce::CriticalSection resultLock;
    std::optional<LoadResult> loadResult;
    std::optional<StretchResult> stretchResult;
    std::optional<RestoreInfo> pendingRestore;
    std::atomic<int> loadGeneration { 0 }, stretchGeneration { 0 };
    bool loading = false;

    std::atomic<double> hostBpm { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplerProcessor)
};

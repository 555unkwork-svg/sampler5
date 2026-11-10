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
    static constexpr double kMaxSeconds = 120.0;   // largo maximo del audio que se carga

    struct Slice
    {
        bool used = false;
        juce::int64 start = 0;
        juce::int64 end = -1;      // -1: termina donde empieza el corte siguiente
        juce::String name;         // vacio: "CHOP n"
        bool favorite = false;     // los favoritos no se pierden al regenerar
    };

    enum class SnapMode { automatic = 0, onset, zeroCrossing, grid };

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
    /** Vuelve a poner el BPM detectado en el audio (pisa lo escrito a mano). */
    void detectBpm();
    double getHostBpm() const { return hostBpm.load(); }
    /** Tempo al que apunta SYNC: el del proyecto si el host lo informa; si no, el manual. */
    bool hasHostTempo() const { return hostBpm.load() > 0.0; }
    double getTargetBpm() const { return hasHostTempo() ? std::round (hostBpm.load() * 100.0) / 100.0 : manualBpm; }
    void setManualBpm (double bpm);

    eng::SliceMode getSliceMode() const { return sliceMode; }
    int getGridIndex() const { return gridIndex; }
    double getSensitivity() const { return sensitivity; }
    void setSensitivity (double newSensitivity);
    void autoSlice (eng::SliceMode mode, int gridIndexToUse);
    void setSliceStart (int pad, juce::int64 position);
    int addSlice (juce::int64 position);   // devuelve el pad usado, o -1 si no queda ninguno libre
    void removeSlice (int pad);
    void setSliceEnd (int pad, juce::int64 position);   // negativo: vuelve a automatico
    juce::int64 getSliceEnd (int pad) const;            // fin real del corte (propio o automatico)
    void setSliceName (int pad, const juce::String& name);
    juce::String getSliceName (int pad) const;
    void setSliceFavorite (int pad, bool favorite);

    /** Zona del audio que se usa para cortar (pestana FUENTE). */
    juce::int64 getRegionStart() const { return regionStart; }
    juce::int64 getRegionEnd() const;
    void setRegion (juce::int64 start, juce::int64 end);

    /** Iman al mover cortes: lleva `position` al punto mas cercano si esta dentro de `tolerance` muestras. */
    bool isSnapEnabled() const { return snapEnabled; }
    SnapMode getSnapMode() const { return snapMode; }
    void setSnap (bool enabled, SnapMode mode);
    juce::int64 snapPosition (juce::int64 position, juce::int64 tolerance) const;

    const eng::Analysis& getAnalysis() const { return analysis; }

    //---- tonalidad
    int getKeyRoot() const { return keyRoot; }        // -1: no detectada
    bool isKeyMinor() const { return keyMinor; }
    int getCurrentKeyRoot() const;                    // la detectada corrida por el pitch global
    juce::String getKeyText() const;                  // "A#m" o "A#m > Cm"
    void setTargetKey (int root);                     // ajusta el pitch global para llegar a esa tonalidad

    //---- presets (kits con ruta al audio)
    static juce::File getPresetFolder();
    juce::StringArray getPresetNames() const;
    bool savePreset (const juce::String& name);
    bool loadPreset (const juce::String& name);
    void loadDefaultPreset();
    juce::String getPresetName() const { return presetName; }

    /** true si el audio va guardado dentro del proyecto. */
    bool hasEmbeddedAudio() const { return embedded != nullptr; }

    int getSelectedPad() const { return selectedPad.load(); }
    /** Seguir pad: el pad tocado por MIDI pasa a ser el elegido (como en una MPC). */
    bool isFollowingPads() const { return followPads; }
    void setFollowPads (bool follow) { followPads = follow; sendChangeMessage(); }
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
        juce::int64 regionStart = 0, regionEnd = -1;
        std::shared_ptr<const juce::MemoryBlock> audioData;   // audio guardado en el proyecto
    };

    struct LoadResult
    {
        int generation = 0;
        juce::File file;
        eng::AudioPtr audio;
        eng::Analysis analysis;
        juce::String error;
        std::optional<RestoreInfo> restore;
        std::shared_ptr<const juce::MemoryBlock> embedded;
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
    double detectedBpm() const;
    std::unique_ptr<juce::XmlElement> buildStateXml();
    void applyStateXml (const juce::XmlElement&, std::shared_ptr<const juce::MemoryBlock> audioData);

    // parametros (punteros crudos, validos mientras viva apvts)
    struct PadParamPtrs
    {
        std::atomic<float>* gain, *pan, *pitch, *attack, *release, *reverse, *gate,
                          * fine, *velAmt, *preGain, *loop, *mute, *filter, *cutoff, *reso;
    };
    std::array<PadParamPtrs, eng::kNumPads> padParams {};
    std::atomic<float>* masterParam = nullptr, *velParam = nullptr, *monoParam = nullptr, *syncParam = nullptr,
                      * stretchParam = nullptr, *tuneParam = nullptr, *keysParam = nullptr, *rootParam = nullptr,
                      * panParam = nullptr, *voicesParam = nullptr, *humanizeParam = nullptr, *swingParam = nullptr,
                      * chokeParam = nullptr, *invParam = nullptr, *midiChParam = nullptr, *midiInParam = nullptr,
                      * rangeLoParam = nullptr, *rangeHiParam = nullptr, *aftertouchParam = nullptr;

    // modelo
    juce::CriticalSection modelLock;      // para get/setStateInformation desde otros hilos
    juce::File sampleFile;
    eng::AudioPtr source;
    eng::Analysis analysis;
    double sampleBpm = 120.0;
    std::array<Slice, eng::kNumPads> slices {};
    eng::SliceMode sliceMode = eng::SliceMode::transients;
    int gridIndex = 2;
    double sensitivity = 0.5;
    double manualBpm = 120.0;
    juce::int64 regionStart = 0, regionEnd = -1;   // -1: hasta el final
    bool snapEnabled = true;
    bool followPads = true;
    int keyRoot = -1;
    bool keyMinor = false;
    std::shared_ptr<const juce::MemoryBlock> embedded;   // el audio en FLAC, para guardarlo en el proyecto
    juce::String presetName;
    SnapMode snapMode = SnapMode::automatic;
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

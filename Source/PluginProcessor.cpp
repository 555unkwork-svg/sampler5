#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Engine/Stretcher.h"

using APVTS = juce::AudioProcessorValueTreeState;

juce::String SamplerProcessor::padParamId (int pad, const char* name)
{
    return "p" + juce::String (pad + 1) + "_" + name;
}

const juce::StringArray& SamplerProcessor::gridNames()
{
    static const juce::StringArray names { "1/4 de tiempo", "1/2 tiempo", "1 tiempo", "2 tiempos", "1 compas (4)" };
    return names;
}

double SamplerProcessor::gridBeatsForIndex (int index)
{
    static const double beats[] = { 0.25, 0.5, 1.0, 2.0, 4.0 };
    return beats[juce::jlimit (0, 4, index)];
}

APVTS::ParameterLayout SamplerProcessor::createLayout()
{
    APVTS::ParameterLayout layout;
    using F = juce::AudioParameterFloat;
    using B = juce::AudioParameterBool;
    using I = juce::AudioParameterInt;
    using Range = juce::NormalisableRange<float>;

    auto timeRange = [] (float maxMs)
    {
        Range r (0.0f, maxMs, 0.1f);
        r.setSkewForCentre (maxMs * 0.1f);
        return r;
    };

    layout.add (std::make_unique<F> (juce::ParameterID { "master", 1 }, "Master", Range (-60.0f, 6.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<F> (juce::ParameterID { "vel", 1 }, "Velocidad", Range (0.0f, 1.0f, 0.01f), 0.5f));
    layout.add (std::make_unique<B> (juce::ParameterID { "mono", 1 }, "Mono", false));
    layout.add (std::make_unique<B> (juce::ParameterID { "sync", 1 }, "Sync", false));
    layout.add (std::make_unique<B> (juce::ParameterID { "stretch", 1 }, "Stretch", true));
    layout.add (std::make_unique<F> (juce::ParameterID { "tune", 1 }, "Tono", Range (-12.0f, 12.0f, 1.0f), 0.0f));
    layout.add (std::make_unique<B> (juce::ParameterID { "keys", 1 }, "Teclas", false));
    layout.add (std::make_unique<I> (juce::ParameterID { "root", 1 }, "Nota base", 0, 127, 60));

    for (int i = 0; i < eng::kNumPads; ++i)
    {
        const auto name = "Pad " + juce::String (i + 1) + " ";
        layout.add (std::make_unique<F> (juce::ParameterID { padParamId (i, "gain"), 1 }, name + "Volumen", Range (-60.0f, 12.0f, 0.1f), 0.0f));
        layout.add (std::make_unique<F> (juce::ParameterID { padParamId (i, "pan"), 1 }, name + "Paneo", Range (-1.0f, 1.0f, 0.01f), 0.0f));
        layout.add (std::make_unique<F> (juce::ParameterID { padParamId (i, "pitch"), 1 }, name + "Tono", Range (-24.0f, 24.0f, 0.01f), 0.0f));
        layout.add (std::make_unique<F> (juce::ParameterID { padParamId (i, "attack"), 1 }, name + "Ataque", timeRange (1000.0f), 1.0f));
        layout.add (std::make_unique<F> (juce::ParameterID { padParamId (i, "release"), 1 }, name + "Release", timeRange (2000.0f), 10.0f));
        layout.add (std::make_unique<B> (juce::ParameterID { padParamId (i, "reverse"), 1 }, name + "Reversa", false));
        layout.add (std::make_unique<B> (juce::ParameterID { padParamId (i, "gate"), 1 }, name + "Mantener", true));
    }

    return layout;
}

SamplerProcessor::SamplerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Params", createLayout())
{
    formats.registerBasicFormats();

    masterParam = apvts.getRawParameterValue ("master");
    velParam = apvts.getRawParameterValue ("vel");
    monoParam = apvts.getRawParameterValue ("mono");
    syncParam = apvts.getRawParameterValue ("sync");
    stretchParam = apvts.getRawParameterValue ("stretch");
    tuneParam = apvts.getRawParameterValue ("tune");
    keysParam = apvts.getRawParameterValue ("keys");
    rootParam = apvts.getRawParameterValue ("root");

    for (int i = 0; i < eng::kNumPads; ++i)
    {
        auto& p = padParams[(size_t) i];
        p.gain = apvts.getRawParameterValue (padParamId (i, "gain"));
        p.pan = apvts.getRawParameterValue (padParamId (i, "pan"));
        p.pitch = apvts.getRawParameterValue (padParamId (i, "pitch"));
        p.attack = apvts.getRawParameterValue (padParamId (i, "attack"));
        p.release = apvts.getRawParameterValue (padParamId (i, "release"));
        p.reverse = apvts.getRawParameterValue (padParamId (i, "reverse"));
        p.gate = apvts.getRawParameterValue (padParamId (i, "gate"));
    }

    startTimerHz (30);
}

SamplerProcessor::~SamplerProcessor()
{
    stopTimer();
    ++loadGeneration;
    ++stretchGeneration;
    pool.removeAllJobs (true, 60000);
    engine.setPlayable (nullptr);
}

//==============================================================================
void SamplerProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
}

bool SamplerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void SamplerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    if (auto* head = getPlayHead())
        if (auto position = head->getPosition())
            if (auto bpm = position->getBpm())
                hostBpm.store (*bpm, std::memory_order_relaxed);

    for (int i = 0; i < eng::kNumPads; ++i)
    {
        const auto& p = padParams[(size_t) i];
        auto& s = engine.pads[(size_t) i];
        s.gainDb = p.gain->load (std::memory_order_relaxed);
        s.pan = p.pan->load (std::memory_order_relaxed);
        s.pitch = p.pitch->load (std::memory_order_relaxed);
        s.attackMs = p.attack->load (std::memory_order_relaxed);
        s.releaseMs = p.release->load (std::memory_order_relaxed);
        s.reverse = p.reverse->load (std::memory_order_relaxed) > 0.5f;
        s.gate = p.gate->load (std::memory_order_relaxed) > 0.5f;
    }

    auto& g = engine.globals;
    g.masterDb = masterParam->load (std::memory_order_relaxed);
    g.velocitySens = velParam->load (std::memory_order_relaxed);
    g.mono = monoParam->load (std::memory_order_relaxed) > 0.5f;
    g.keysMode = keysParam->load (std::memory_order_relaxed) > 0.5f;
    g.keysRoot = (int) std::lround (rootParam->load (std::memory_order_relaxed));
    g.selectedPad = selectedPad.load (std::memory_order_relaxed);

    engine.process (buffer, midi);
}

juce::AudioProcessorEditor* SamplerProcessor::createEditor()
{
    return new SamplerEditor (*this);
}

//==============================================================================
// Carga de audio

void SamplerProcessor::loadFile (const juce::File& file)
{
    startLoad (file, std::nullopt);
}

void SamplerProcessor::startLoad (const juce::File& file, std::optional<RestoreInfo> restore)
{
    const int generation = ++loadGeneration;
    loading = true;
    errorText.clear();
    sendChangeMessage();

    pool.addJob ([this, file, generation, restore = std::move (restore)]
    {
        LoadResult result;
        result.generation = generation;
        result.file = file;
        result.restore = restore;

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        {
            result.error = file.existsAsFile() ? "No se pudo leer el audio: " + file.getFileName()
                                               : "No se encontro el archivo: " + file.getFullPathName();
        }
        else
        {
            const auto maxSamples = (juce::int64) (kMaxSeconds * reader->sampleRate);
            const int length = (int) std::min (reader->lengthInSamples, maxSamples);
            const int chans = (int) juce::jlimit (1u, 2u, reader->numChannels);

            auto data = std::make_shared<eng::AudioData>();
            data->sampleRate = reader->sampleRate;
            data->buffer.setSize (chans, length);
            reader->read (&data->buffer, 0, length, 0, true, chans > 1);

            if (generation == loadGeneration.load())
                result.analysis = eng::analyse (data->buffer, data->sampleRate);
            result.audio = std::move (data);
        }

        const juce::ScopedLock lock (resultLock);
        loadResult = std::move (result);
    });
}

void SamplerProcessor::finishLoad (LoadResult& result)
{
    loading = false;

    if (result.audio == nullptr)
    {
        errorText = result.error;
        if (result.restore.has_value())
        {
            // proyecto guardado cuyo audio ya no esta: recordamos donde estaba
            const juce::ScopedLock lock (modelLock);
            sampleFile = result.file;
            sampleBpm = result.restore->bpm;
            slices = result.restore->slices;
        }
        sendChangeMessage();
        return;
    }

    ++stretchGeneration;   // cualquier stretch en curso era del audio anterior
    stretched.reset();
    stretchRunning = false;

    {
        const juce::ScopedLock lock (modelLock);
        sampleFile = result.file;
        source = result.audio;
        analysis = std::move (result.analysis);

        if (result.restore.has_value())
        {
            sampleBpm = result.restore->bpm > 0.0 ? result.restore->bpm : 120.0;
            slices = result.restore->slices;
            for (auto& s : slices)
                s.start = juce::jlimit<juce::int64> (0, source->buffer.getNumSamples() - 1, s.start);
        }
        else
        {
            sampleBpm = analysis.bpm > 0.0 ? analysis.bpm : 120.0;

            // si el nombre del archivo trae el tempo y es coherente con lo detectado, le creemos
            const double named = eng::bpmFromFileName (sampleFile.getFileNameWithoutExtension());
            if (named > 0.0)
            {
                bool coherent = analysis.bpm <= 0.0;
                for (double ratio : { 1.0, 0.5, 2.0, 0.75, 4.0 / 3.0, 2.0 / 3.0, 1.5 })
                    if (std::abs (analysis.bpm * ratio - named) < named * 0.03)
                        coherent = true;
                if (coherent)
                    sampleBpm = named;
            }
        }
    }

    if (! result.restore.has_value())
        autoSlice (sliceMode, gridIndex);   // tambien arma el Playable
    else
        rebuildPlayable();

    sendChangeMessage();
}

//==============================================================================
// Cortes

void SamplerProcessor::autoSlice (eng::SliceMode mode, int gridIndexToUse)
{
    sliceMode = mode;
    gridIndex = gridIndexToUse;
    if (source == nullptr)
        return;

    const auto points = eng::makeSlices (mode, analysis, source->buffer.getNumSamples(), source->sampleRate,
                                         sampleBpm, gridBeatsForIndex (gridIndex),
                                         juce::Time::currentTimeMillis(), sensitivity);
    {
        const juce::ScopedLock lock (modelLock);
        for (int i = 0; i < eng::kNumPads; ++i)
        {
            auto& s = slices[(size_t) i];
            s.used = i < (int) points.size();
            s.start = s.used ? points[(size_t) i] : 0;
        }
    }
    rebuildPlayable();
    sendChangeMessage();
}

void SamplerProcessor::setSensitivity (double newSensitivity)
{
    newSensitivity = juce::jlimit (0.0, 1.0, newSensitivity);
    if (std::abs (newSensitivity - sensitivity) < 1.0e-4)
        return;
    sensitivity = newSensitivity;
    if (sliceMode == eng::SliceMode::transients)
        autoSlice (sliceMode, gridIndex);
}

void SamplerProcessor::setSliceStart (int pad, juce::int64 position)
{
    if (source == nullptr || pad < 0 || pad >= eng::kNumPads)
        return;
    {
        const juce::ScopedLock lock (modelLock);
        slices[(size_t) pad].used = true;
        slices[(size_t) pad].start = juce::jlimit<juce::int64> (0, source->buffer.getNumSamples() - 1, position);
    }
    rebuildPlayable();
    sendChangeMessage();
}

int SamplerProcessor::addSlice (juce::int64 position)
{
    if (source == nullptr)
        return -1;
    for (int i = 0; i < eng::kNumPads; ++i)
        if (! slices[(size_t) i].used)
        {
            setSliceStart (i, position);
            return i;
        }
    return -1;
}

void SamplerProcessor::removeSlice (int pad)
{
    if (pad < 0 || pad >= eng::kNumPads)
        return;
    {
        const juce::ScopedLock lock (modelLock);
        slices[(size_t) pad].used = false;
    }
    rebuildPlayable();
    sendChangeMessage();
}

void SamplerProcessor::setSelectedPad (int pad)
{
    selectedPad.store (juce::jlimit (0, eng::kNumPads - 1, pad));
    sendChangeMessage();
}

void SamplerProcessor::setSampleBpm (double bpm)
{
    {
        const juce::ScopedLock lock (modelLock);
        sampleBpm = juce::jlimit (30.0, 300.0, bpm);
    }
    updateTempoTarget();
    sendChangeMessage();
}

//==============================================================================
// Sync / stretch

double SamplerProcessor::currentSpeed() const
{
    const double host = std::round (hostBpm.load() * 100.0) / 100.0;
    if (syncParam->load() < 0.5f || host <= 0.0 || sampleBpm <= 0.0)
        return 1.0;
    return juce::jlimit (0.5, 2.0, host / sampleBpm);
}

void SamplerProcessor::rebuildPlayable()
{
    if (source == nullptr)
    {
        engine.setPlayable (nullptr);
        return;
    }

    const StretchKey want { currentSpeed(), (double) tuneParam->load() };
    const bool stretchOn = stretchParam->load() > 0.5f;
    const bool needsStretch = stretchOn && ! (want == StretchKey {});
    appliedKey = want;
    appliedStretchOn = stretchOn;

    auto p = std::make_shared<eng::Playable>();
    p->sourceLength = source->buffer.getNumSamples();

    if (needsStretch && stretched != nullptr && stretchedKey == want)
    {
        p->audio = stretched;
        p->posScale = (double) stretched->buffer.getNumSamples() / (double) p->sourceLength;
        p->rateScale = 1.0;
    }
    else
    {
        // sin stretch (o mientras se calcula): cambia la velocidad y con ella el tono, como un vinilo
        p->audio = source;
        p->posScale = 1.0;
        p->rateScale = want.speed * std::pow (2.0, want.tune / 12.0);
    }

    for (int i = 0; i < eng::kNumPads; ++i)
    {
        const auto& s = slices[(size_t) i];
        p->used[(size_t) i] = s.used;
        p->start[(size_t) i] = s.start;

        // cada corte termina donde empieza el siguiente
        juce::int64 end = p->sourceLength;
        for (const auto& other : slices)
            if (other.used && other.start > s.start)
                end = std::min (end, other.start);
        p->end[(size_t) i] = end;
    }

    engine.setPlayable (std::move (p));
}

void SamplerProcessor::updateTempoTarget()
{
    if (source == nullptr)
        return;

    const StretchKey want { currentSpeed(), (double) tuneParam->load() };
    const bool stretchOn = stretchParam->load() > 0.5f;

    if (! (want == appliedKey) || stretchOn != appliedStretchOn)
        rebuildPlayable();

    // esperamos a que el tempo se quede quieto antes de recalcular el stretch
    if (want == lastSeenKey)
        ++stableTicks;
    else
        stableTicks = 0;
    lastSeenKey = want;

    const bool needsStretch = stretchOn && ! (want == StretchKey {});
    const bool haveIt = stretched != nullptr && stretchedKey == want;
    const bool alreadyAsked = stretchRunning && requestedKey == want;

    if (needsStretch && ! haveIt && ! alreadyAsked && stableTicks >= 6)
    {
        const int generation = ++stretchGeneration;
        requestedKey = want;
        stretchRunning = true;
        sendChangeMessage();

        pool.addJob ([this, generation, want, src = source]
        {
            if (generation != stretchGeneration.load())
                return;

            auto data = std::make_shared<eng::AudioData>();
            data->sampleRate = src->sampleRate;
            data->buffer = eng::stretchOffline (src->buffer, src->sampleRate, want.speed, want.tune);

            const juce::ScopedLock lock (resultLock);
            stretchResult = StretchResult { generation, want, std::move (data) };
        });
    }
}

void SamplerProcessor::timerCallback()
{
    std::optional<LoadResult> loaded;
    std::optional<StretchResult> stretchDone;
    std::optional<RestoreInfo> restore;
    {
        const juce::ScopedLock lock (resultLock);
        loaded.swap (loadResult);
        stretchDone.swap (stretchResult);
        restore.swap (pendingRestore);
    }

    if (restore.has_value())
    {
        if (restore->file == juce::File())
        {
            // estado sin audio
            ++loadGeneration;
            ++stretchGeneration;
            loading = false;
            stretchRunning = false;
            source.reset();
            stretched.reset();
            {
                const juce::ScopedLock lock (modelLock);
                sampleFile = juce::File();
                slices = restore->slices;
            }
            rebuildPlayable();
            sendChangeMessage();
        }
        else
        {
            const auto file = restore->file;
            startLoad (file, std::move (restore));
        }
    }

    if (loaded.has_value() && loaded->generation == loadGeneration.load())
        finishLoad (*loaded);

    if (stretchDone.has_value() && stretchDone->generation == stretchGeneration.load())
    {
        stretched = stretchDone->audio;
        stretchedKey = stretchDone->key;
        stretchRunning = false;
        rebuildPlayable();
        sendChangeMessage();
    }

    updateTempoTarget();
    engine.purgeRetired();
}

juce::String SamplerProcessor::getStatusText() const
{
    if (loading)
        return "Cargando y analizando...";
    if (errorText.isNotEmpty())
        return errorText;
    if (source == nullptr)
        return "Arrastra un audio aca o usa Cargar";
    if (stretchRunning)
        return "Calculando stretch...";

    const double seconds = source->buffer.getNumSamples() / source->sampleRate;
    juce::String text = juce::String (seconds, 1) + " s";
    if (syncParam->load() > 0.5f)
    {
        const double host = hostBpm.load();
        text << (host > 0.0 ? "  |  proyecto " + juce::String (host, 1) + " BPM, velocidad x" + juce::String (currentSpeed(), 2)
                            : "  |  SYNC sin efecto: no llega el tempo del proyecto");
    }
    return text;
}

//==============================================================================
// Estado guardado. El audio se guarda como ruta al archivo, no adentro del proyecto.

void SamplerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement root ("Sampler5State");
    root.setAttribute ("version", 1);

    if (auto params = apvts.copyState().createXml())
        root.addChildElement (params.release());

    {
        const juce::ScopedLock lock (modelLock);
        root.setAttribute ("file", sampleFile.getFullPathName());
        root.setAttribute ("bpm", sampleBpm);
        root.setAttribute ("sliceMode", (int) sliceMode);
        root.setAttribute ("grid", gridIndex);
        root.setAttribute ("sens", sensitivity);
        root.setAttribute ("selectedPad", selectedPad.load());

        auto* pads = root.createNewChildElement ("Pads");
        for (int i = 0; i < eng::kNumPads; ++i)
        {
            auto* pad = pads->createNewChildElement ("Pad");
            pad->setAttribute ("used", slices[(size_t) i].used);
            pad->setAttribute ("start", juce::String (slices[(size_t) i].start));
            pad->setAttribute ("note", engine.padNote[(size_t) i].load());
        }
    }

    copyXmlToBinary (root, destData);
}

void SamplerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto root = getXmlFromBinary (data, sizeInBytes);
    if (root == nullptr || ! root->hasTagName ("Sampler5State"))
        return;

    if (auto* params = root->getChildByName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*params));

    RestoreInfo info;
    const auto path = root->getStringAttribute ("file");
    if (path.isNotEmpty() && juce::File::isAbsolutePath (path))
        info.file = juce::File (path);
    info.bpm = root->getDoubleAttribute ("bpm", 120.0);

    {
        const juce::ScopedLock lock (modelLock);
        sliceMode = (eng::SliceMode) juce::jlimit (0, 4, root->getIntAttribute ("sliceMode", 0));
        gridIndex = juce::jlimit (0, 4, root->getIntAttribute ("grid", 2));
        sensitivity = juce::jlimit (0.0, 1.0, root->getDoubleAttribute ("sens", 0.5));
    }
    selectedPad.store (juce::jlimit (0, eng::kNumPads - 1, root->getIntAttribute ("selectedPad", 0)));

    if (auto* pads = root->getChildByName ("Pads"))
    {
        int i = 0;
        for (auto* pad : pads->getChildWithTagNameIterator ("Pad"))
        {
            if (i >= eng::kNumPads)
                break;
            info.slices[(size_t) i].used = pad->getBoolAttribute ("used", false);
            info.slices[(size_t) i].start = pad->getStringAttribute ("start", "0").getLargeIntValue();
            engine.padNote[(size_t) i].store (juce::jlimit (-1, 127, pad->getIntAttribute ("note", eng::SamplerEngine::kDefaultFirstNote + i)));
            ++i;
        }
    }

    // el resto se aplica en el hilo de mensajes (ver timerCallback)
    const juce::ScopedLock lock (resultLock);
    pendingRestore = std::move (info);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SamplerProcessor();
}

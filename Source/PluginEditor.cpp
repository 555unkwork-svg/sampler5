#include "PluginEditor.h"

namespace
{
const juce::Colour kBg (0xff1d1f23), kPanel (0xff282b31), kPad (0xff3a3e46), kPadEmpty (0xff2c2f35),
                   kAccent (0xffff6a2b), kText (0xffe8e8e6), kDim (0xff8d9199), kWave (0xffb8bcc4);

juce::String noteName (int note)
{
    // FL Studio llama C5 a la nota 60
    return note < 0 ? "--" : juce::MidiMessage::getMidiNoteName (note, true, true, 5);
}
}

//==============================================================================
WaveformView::WaveformView (SamplerProcessor& p) : proc (p) {}

float WaveformView::xForSample (juce::int64 sample) const
{
    const auto src = proc.getSource();
    if (src == nullptr || src->buffer.getNumSamples() == 0)
        return 0.0f;
    return (float) ((double) sample / src->buffer.getNumSamples() * getWidth());
}

juce::int64 WaveformView::sampleForX (float x) const
{
    const auto src = proc.getSource();
    if (src == nullptr || getWidth() <= 0)
        return 0;
    return (juce::int64) (juce::jlimit (0.0, 1.0, (double) x / getWidth()) * src->buffer.getNumSamples());
}

int WaveformView::markerAt (float x) const
{
    int best = -1;
    float bestDist = 7.0f;
    const auto& slices = proc.getSlices();
    for (int i = 0; i < eng::kNumPads; ++i)
    {
        if (! slices[(size_t) i].used)
            continue;
        const float d = std::abs (xForSample (slices[(size_t) i].start) - x);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

void WaveformView::rebuildPeaks()
{
    const auto src = proc.getSource();
    peaksFor = src.get();
    peaks.assign ((size_t) std::max (0, getWidth()), { 0.0f, 0.0f });
    if (src == nullptr || getWidth() <= 0)
        return;

    const auto& buffer = src->buffer;
    const int n = buffer.getNumSamples();
    for (int x = 0; x < getWidth(); ++x)
    {
        const int from = (int) ((juce::int64) n * x / getWidth());
        const int to = std::max (from + 1, (int) ((juce::int64) n * (x + 1) / getWidth()));
        float lo = 0.0f, hi = 0.0f;
        for (int c = 0; c < buffer.getNumChannels(); ++c)
        {
            const auto range = buffer.findMinMax (c, from, std::min (to, n) - from);
            lo = std::min (lo, range.getStart());
            hi = std::max (hi, range.getEnd());
        }
        peaks[(size_t) x] = { lo, hi };
    }
}

void WaveformView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff14161a));
    const auto src = proc.getSource();

    if (src == nullptr)
    {
        g.setColour (kDim);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (proc.getStatusText(), getLocalBounds(), juce::Justification::centred);
        return;
    }

    if (peaksFor != src.get() || (int) peaks.size() != getWidth())
        rebuildPeaks();

    const auto& slices = proc.getSlices();
    const int selected = proc.getSelectedPad();
    const float h = (float) getHeight();
    const float mid = h * 0.5f;

    // zona del pad elegido
    if (slices[(size_t) selected].used)
    {
        juce::int64 end = src->buffer.getNumSamples();
        for (const auto& s : slices)
            if (s.used && s.start > slices[(size_t) selected].start)
                end = std::min (end, s.start);
        const float x0 = xForSample (slices[(size_t) selected].start);
        g.setColour (kAccent.withAlpha (0.16f));
        g.fillRect (x0, 0.0f, xForSample (end) - x0, h);
    }

    g.setColour (kWave);
    for (int x = 0; x < (int) peaks.size(); ++x)
    {
        const float top = mid - peaks[(size_t) x].second * mid * 0.95f;
        const float bottom = mid - peaks[(size_t) x].first * mid * 0.95f;
        g.fillRect ((float) x, top, 1.0f, std::max (1.0f, bottom - top));
    }

    g.setFont (juce::FontOptions (11.0f));
    for (int i = 0; i < eng::kNumPads; ++i)
    {
        if (! slices[(size_t) i].used)
            continue;
        const float x = xForSample (slices[(size_t) i].start);
        const bool isSelected = i == selected;
        g.setColour (isSelected ? kAccent : kText.withAlpha (0.8f));
        g.fillRect (x, 0.0f, isSelected ? 2.0f : 1.0f, h);
        const juce::Rectangle<float> flag (x, 0.0f, 18.0f, 14.0f);
        g.fillRect (flag);
        g.setColour (juce::Colours::black);
        g.drawText (juce::String (i + 1), flag, juce::Justification::centred);
    }

    g.setColour (juce::Colours::white);
    for (int i = 0; i < eng::kNumPads; ++i)
    {
        const float pos = proc.engine.padPlayPos[(size_t) i].load (std::memory_order_relaxed);
        if (pos >= 0.0f && pos <= 1.0f)
            g.fillRect (pos * (float) getWidth(), 0.0f, 1.0f, h);
    }
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (markerAt ((float) e.x) >= 0 ? juce::MouseCursor::LeftRightResizeCursor
                                                : juce::MouseCursor::NormalCursor);
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    const int marker = markerAt ((float) e.x);
    if (marker < 0)
        return;

    if (e.mods.isPopupMenu())
    {
        proc.removeSlice (marker);
        return;
    }

    dragging = marker;
    proc.setSelectedPad (marker);
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging >= 0)
        proc.setSliceStart (dragging, sampleForX ((float) e.x));
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    dragging = -1;
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (markerAt ((float) e.x) >= 0)
        return;
    const int pad = proc.addSlice (sampleForX ((float) e.x));
    if (pad >= 0)
        proc.setSelectedPad (pad);
}

//==============================================================================
PadGrid::PadGrid (SamplerProcessor& p) : proc (p) {}

juce::Rectangle<int> PadGrid::padBounds (int pad) const
{
    const int w = getWidth() / 4, h = getHeight() / 4;
    return juce::Rectangle<int> ((pad % 4) * w, (pad / 4) * h, w, h).reduced (4);
}

int PadGrid::padAt (juce::Point<int> p) const
{
    for (int i = 0; i < eng::kNumPads; ++i)
        if (padBounds (i).contains (p))
            return i;
    return -1;
}

void PadGrid::paint (juce::Graphics& g)
{
    const auto& slices = proc.getSlices();
    const int selected = proc.getSelectedPad();
    const int learning = proc.engine.learnPad.load();

    for (int i = 0; i < eng::kNumPads; ++i)
    {
        const auto r = padBounds (i).toFloat();
        const bool used = slices[(size_t) i].used;
        const bool playing = proc.engine.padPlayPos[(size_t) i].load (std::memory_order_relaxed) >= 0.0f;

        g.setColour (playing ? kAccent : (used ? kPad : kPadEmpty));
        g.fillRoundedRectangle (r, 5.0f);

        if (i == selected)
        {
            g.setColour (i == learning ? juce::Colours::yellow : kAccent);
            g.drawRoundedRectangle (r.reduced (1.0f), 5.0f, 2.0f);
        }

        g.setColour (playing ? juce::Colours::black : (used ? kText : kDim));
        g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        g.drawText (juce::String (i + 1), r.reduced (8.0f, 6.0f), juce::Justification::topLeft);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (used ? noteName (proc.engine.padNote[(size_t) i].load()) : juce::String ("VACIO"),
                    r.reduced (8.0f, 6.0f), juce::Justification::bottomLeft);
    }
}

void PadGrid::mouseDown (const juce::MouseEvent& e)
{
    const int pad = padAt (e.getPosition());
    if (pad < 0)
        return;
    held = pad;
    proc.setSelectedPad (pad);
    proc.engine.triggerPadFromUI (pad, true);
}

void PadGrid::mouseUp (const juce::MouseEvent&)
{
    if (held >= 0)
        proc.engine.triggerPadFromUI (held, false);
    held = -1;
}

//==============================================================================
SamplerEditor::SamplerEditor (SamplerProcessor& p)
    : AudioProcessorEditor (&p), proc (p), waveform (p), padGrid (p)
{
    auto& lf = getLookAndFeel();
    lf.setColour (juce::Slider::rotarySliderFillColourId, kAccent);
    lf.setColour (juce::Slider::thumbColourId, kText);
    lf.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lf.setColour (juce::TextButton::buttonColourId, kPad);
    lf.setColour (juce::TextButton::buttonOnColourId, kAccent);
    lf.setColour (juce::ComboBox::backgroundColourId, kPad);
    lf.setColour (juce::ToggleButton::tickColourId, kAccent);
    lf.setColour (juce::Label::textColourId, kText);

    addAndMakeVisible (waveform);
    addAndMakeVisible (padGrid);

    for (auto* b : { &loadButton, &halfButton, &doubleButton, &sliceButton, &learnButton })
        addAndMakeVisible (*b);
    for (auto* b : { &syncButton, &stretchButton, &monoButton, &keysButton, &reverseButton, &gateButton })
        addAndMakeVisible (*b);
    for (auto* l : { &fileLabel, &bpmCaption, &bpmLabel, &statusLabel, &padTitle, &modeCaption, &gridCaption })
        addAndMakeVisible (*l);
    addAndMakeVisible (modeBox);
    addAndMakeVisible (gridBox);

    bpmCaption.setText ("BPM", juce::dontSendNotification);
    modeCaption.setText ("Cortar por", juce::dontSendNotification);
    gridCaption.setText ("Grilla", juce::dontSendNotification);
    for (auto* l : { &bpmCaption, &modeCaption, &gridCaption, &statusLabel })
        l->setColour (juce::Label::textColourId, kDim);
    statusLabel.setJustificationType (juce::Justification::centredRight);
    padTitle.setFont (juce::FontOptions (16.0f, juce::Font::bold));

    bpmLabel.setEditable (true);
    bpmLabel.setColour (juce::Label::backgroundColourId, kPad);
    bpmLabel.setJustificationType (juce::Justification::centred);
    bpmLabel.onTextChange = [this]
    {
        const double v = bpmLabel.getText().replaceCharacter (',', '.').getDoubleValue();
        if (v > 0.0)
            proc.setSampleBpm (v);
        refresh();
    };
    halfButton.onClick = [this] { proc.setSampleBpm (proc.getSampleBpm() * 0.5); };
    doubleButton.onClick = [this] { proc.setSampleBpm (proc.getSampleBpm() * 2.0); };

    modeBox.addItemList ({ "Golpes", "Grilla", "Partes iguales", "Al azar", "A mano (vacio)" }, 1);
    gridBox.addItemList (SamplerProcessor::gridNames(), 1);
    modeBox.setSelectedItemIndex ((int) proc.getSliceMode(), juce::dontSendNotification);
    gridBox.setSelectedItemIndex (proc.getGridIndex(), juce::dontSendNotification);
    auto reslice = [this]
    {
        proc.autoSlice ((eng::SliceMode) modeBox.getSelectedItemIndex(), gridBox.getSelectedItemIndex());
    };
    modeBox.onChange = reslice;
    gridBox.onChange = [this, reslice] { if (modeBox.getSelectedItemIndex() == (int) eng::SliceMode::grid) reslice(); };
    sliceButton.onClick = reslice;

    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Elegir un audio", juce::File(),
                                                       "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile())
                                      proc.loadFile (fc.getResult());
                              });
    };

    learnButton.setClickingTogglesState (true);
    learnButton.onClick = [this]
    {
        proc.engine.learnPad.store (learnButton.getToggleState() ? proc.getSelectedPad() : -1);
        padGrid.repaint();
    };

    setupKnob (tune, "Tono", " st");
    setupKnob (master, "Master", " dB");
    setupKnob (vel, "Velocidad", "");
    setupKnob (root, "Nota base", "");
    setupKnob (padGain, "Volumen", " dB");
    setupKnob (padPan, "Paneo", "");
    setupKnob (padPitch, "Tono", " st");
    setupKnob (padAttack, "Ataque", " ms");
    setupKnob (padRelease, "Release", " ms");

    tune.attachment = std::make_unique<SliderAttachment> (proc.apvts, "tune", tune.slider);
    master.attachment = std::make_unique<SliderAttachment> (proc.apvts, "master", master.slider);
    vel.attachment = std::make_unique<SliderAttachment> (proc.apvts, "vel", vel.slider);
    root.attachment = std::make_unique<SliderAttachment> (proc.apvts, "root", root.slider);
    // despues del attachment, que si no pisa el texto
    root.slider.textFromValueFunction = [] (double v) { return noteName ((int) std::lround (v)); };
    root.slider.updateText();
    syncAtt = std::make_unique<ButtonAttachment> (proc.apvts, "sync", syncButton);
    stretchAtt = std::make_unique<ButtonAttachment> (proc.apvts, "stretch", stretchButton);
    monoAtt = std::make_unique<ButtonAttachment> (proc.apvts, "mono", monoButton);
    keysAtt = std::make_unique<ButtonAttachment> (proc.apvts, "keys", keysButton);

    proc.addChangeListener (this);
    refresh();
    startTimerHz (30);
    setSize (940, 640);
}

SamplerEditor::~SamplerEditor()
{
    proc.removeChangeListener (this);
    proc.engine.learnPad.store (-1);
}

void SamplerEditor::setupKnob (Knob& k, const juce::String& name, const juce::String& suffix)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    k.slider.setTextValueSuffix (suffix);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, kDim);
    k.label.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
}

void SamplerEditor::layoutKnob (Knob& k, juce::Rectangle<int> area)
{
    k.label.setBounds (area.removeFromTop (16));
    k.slider.setBounds (area);
}

void SamplerEditor::attachPad (int pad)
{
    if (pad == attachedPad)
        return;
    attachedPad = pad;

    // primero soltamos los anteriores: un control no puede tener dos a la vez
    padGain.attachment.reset();
    padPan.attachment.reset();
    padPitch.attachment.reset();
    padAttack.attachment.reset();
    padRelease.attachment.reset();
    reverseAtt.reset();
    gateAtt.reset();

    auto id = [pad] (const char* name) { return SamplerProcessor::padParamId (pad, name); };
    padGain.attachment = std::make_unique<SliderAttachment> (proc.apvts, id ("gain"), padGain.slider);
    padPan.attachment = std::make_unique<SliderAttachment> (proc.apvts, id ("pan"), padPan.slider);
    padPitch.attachment = std::make_unique<SliderAttachment> (proc.apvts, id ("pitch"), padPitch.slider);
    padAttack.attachment = std::make_unique<SliderAttachment> (proc.apvts, id ("attack"), padAttack.slider);
    padRelease.attachment = std::make_unique<SliderAttachment> (proc.apvts, id ("release"), padRelease.slider);
    reverseAtt = std::make_unique<ButtonAttachment> (proc.apvts, id ("reverse"), reverseButton);
    gateAtt = std::make_unique<ButtonAttachment> (proc.apvts, id ("gate"), gateButton);
}

void SamplerEditor::refresh()
{
    const int pad = proc.getSelectedPad();
    attachPad (pad);

    fileLabel.setText (proc.getFileName().isEmpty() ? juce::String ("Sin audio") : proc.getFileName(),
                       juce::dontSendNotification);
    if (! bpmLabel.isBeingEdited())
        bpmLabel.setText (juce::String (proc.getSampleBpm(), 2), juce::dontSendNotification);
    statusLabel.setText (proc.getStatusText(), juce::dontSendNotification);
    padTitle.setText ("PAD " + juce::String (pad + 1) + "   nota " + noteName (proc.engine.padNote[(size_t) pad].load()),
                      juce::dontSendNotification);
    modeBox.setSelectedItemIndex ((int) proc.getSliceMode(), juce::dontSendNotification);
    gridBox.setSelectedItemIndex (proc.getGridIndex(), juce::dontSendNotification);

    if (proc.engine.learnPad.load() >= 0 && proc.engine.learnPad.load() != pad)
        proc.engine.learnPad.store (pad);   // el aprendizaje sigue al pad elegido

    repaint();
}

void SamplerEditor::timerCallback()
{
    if (proc.engine.lastLearnedPad.exchange (-1) >= 0 || (learnButton.getToggleState() && proc.engine.learnPad.load() < 0))
    {
        learnButton.setToggleState (false, juce::dontSendNotification);
        refresh();
    }

    statusLabel.setText (proc.getStatusText(), juce::dontSendNotification);
    waveform.repaint();
    padGrid.repaint();
}

void SamplerEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    if (files.size() > 0)
        proc.loadFile (juce::File (files[0]));
    repaint();
}

void SamplerEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kPanel);
    for (auto* c : { (juce::Component*) &padGrid })
        g.fillRoundedRectangle (c->getBounds().expanded (6).toFloat(), 6.0f);

    auto right = padGrid.getBounds().expanded (6);
    right.setLeft (padGrid.getRight() + 18);
    right.setRight (getWidth() - 12);
    g.fillRoundedRectangle (right.toFloat(), 6.0f);
}

void SamplerEditor::paintOverChildren (juce::Graphics& g)
{
    if (dragOver)
    {
        g.setColour (kAccent);
        g.drawRect (getLocalBounds(), 3);
    }
}

void SamplerEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto top = area.removeFromTop (30);
    loadButton.setBounds (top.removeFromLeft (80));
    top.removeFromLeft (8);
    stretchButton.setBounds (top.removeFromRight (95));
    syncButton.setBounds (top.removeFromRight (70));
    top.removeFromRight (10);
    doubleButton.setBounds (top.removeFromRight (34));
    top.removeFromRight (4);
    halfButton.setBounds (top.removeFromRight (34));
    top.removeFromRight (4);
    bpmLabel.setBounds (top.removeFromRight (70));
    bpmCaption.setBounds (top.removeFromRight (40));
    fileLabel.setBounds (top);

    area.removeFromTop (8);
    waveform.setBounds (area.removeFromTop (170));
    area.removeFromTop (8);

    auto sliceRow = area.removeFromTop (28);
    modeCaption.setBounds (sliceRow.removeFromLeft (75));
    modeBox.setBounds (sliceRow.removeFromLeft (150));
    sliceRow.removeFromLeft (12);
    gridCaption.setBounds (sliceRow.removeFromLeft (45));
    gridBox.setBounds (sliceRow.removeFromLeft (130));
    sliceRow.removeFromLeft (12);
    sliceButton.setBounds (sliceRow.removeFromLeft (80));
    statusLabel.setBounds (sliceRow);

    area.removeFromTop (14);
    area.reduce (6, 0);
    area.removeFromBottom (6);

    padGrid.setBounds (area.removeFromLeft (470));
    area.removeFromLeft (30);

    // panel derecho: pad elegido arriba, global abajo
    padTitle.setBounds (area.removeFromTop (24));
    area.removeFromTop (4);

    auto row1 = area.removeFromTop (100);
    const int kw = row1.getWidth() / 5;
    layoutKnob (padGain, row1.removeFromLeft (kw));
    layoutKnob (padPan, row1.removeFromLeft (kw));
    layoutKnob (padPitch, row1.removeFromLeft (kw));
    layoutKnob (padAttack, row1.removeFromLeft (kw));
    layoutKnob (padRelease, row1);

    area.removeFromTop (8);
    auto row2 = area.removeFromTop (26);
    reverseButton.setBounds (row2.removeFromLeft (95));
    gateButton.setBounds (row2.removeFromLeft (100));
    learnButton.setBounds (row2.removeFromRight (120));

    area.removeFromTop (20);
    auto row3 = area.removeFromTop (100);
    layoutKnob (master, row3.removeFromLeft (kw));
    layoutKnob (vel, row3.removeFromLeft (kw));
    layoutKnob (tune, row3.removeFromLeft (kw));
    layoutKnob (root, row3.removeFromLeft (kw));

    area.removeFromTop (8);
    auto row4 = area.removeFromTop (26);
    monoButton.setBounds (row4.removeFromLeft (95));
    keysButton.setBounds (row4.removeFromLeft (100));
}

#include "PluginEditor.h"

namespace
{
// Paleta
const juce::Colour kBody (0xffd6d7d2), kBodyLine (0xffb4b6b0), kInk (0xff1b1c1e), kInkDim (0xff6c6f6b),
                   kKeyDark (0xff2a2b2d), kKeyGrey (0xff9a9d98), kKeyLight (0xfff1f1ec),
                   kOrange (0xffff5a1f), kDisplay (0xff0d0e0f), kLcd (0xffeceae2), kLcdDim (0xff8b8e88),
                   kLedOff (0xff4b4d4f);

juce::FontOptions sans (float height, bool bold = true)
{
    return juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain).withKerningFactor (0.03f);
}

juce::FontOptions mono (float height)
{
    return juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::bold);
}

juce::String noteName (int note)
{
    // FL Studio llama C5 a la nota 60
    return note < 0 ? "--" : juce::MidiMessage::getMidiNoteName (note, true, true, 5);
}

/** Dibuja una tecla con relieve y devuelve el rectangulo de su cara superior. */
juce::Rectangle<float> drawKey (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour,
                                bool down, float radius = 5.0f)
{
    const float travel = 3.0f;
    area = area.reduced (1.0f);
    auto face = area.withTrimmedBottom (travel);
    if (down)
        face = face.translated (0.0f, travel - 1.0f);

    g.setColour (colour.darker (0.75f).withMultipliedAlpha (0.9f));
    g.fillRoundedRectangle (area.withTrimmedTop (travel), radius);

    g.setColour (colour);
    g.fillRoundedRectangle (face, radius);
    g.setColour (juce::Colours::white.withAlpha (0.14f));
    g.drawRoundedRectangle (face.reduced (0.75f), radius, 1.0f);
    return face;
}

void drawLed (juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour, bool on)
{
    if (on)
    {
        g.setColour (colour.withAlpha (0.25f));
        g.fillEllipse (juce::Rectangle<float> (radius * 4.0f, radius * 4.0f).withCentre (centre));
    }
    g.setColour (on ? colour : kLedOff);
    g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
}
}

//==============================================================================
HardwareLook::HardwareLook()
{
    setColour (juce::ResizableWindow::backgroundColourId, kBody);
    setColour (juce::Label::textColourId, kInk);
    setColour (juce::Label::backgroundWhenEditingColourId, kDisplay);
    setColour (juce::Label::textWhenEditingColourId, kLcd);
    setColour (juce::Label::outlineWhenEditingColourId, kOrange);
    setColour (juce::TextEditor::highlightColourId, kOrange.withAlpha (0.6f));
    setColour (juce::TextEditor::highlightedTextColourId, juce::Colours::white);
    setColour (juce::CaretComponent::caretColourId, kOrange);

    setColour (juce::TextButton::buttonColourId, kKeyDark);
    setColour (juce::TextButton::buttonOnColourId, kOrange);
    setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ToggleButton::textColourId, kInk);

    setColour (juce::Slider::thumbColourId, kKeyDark);
    setColour (juce::Slider::textBoxTextColourId, kInk);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, kOrange.withAlpha (0.6f));

    setColour (juce::ComboBox::textColourId, kInk);
    setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::PopupMenu::backgroundColourId, kKeyLight);
    setColour (juce::PopupMenu::textColourId, kInk);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kOrange);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
}

void HardwareLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& colour, bool over, bool down)
{
    drawKey (g, b.getLocalBounds().toFloat(), over && ! down ? colour.brighter (0.12f) : colour, down);
}

juce::Font HardwareLook::getTextButtonFont (juce::TextButton&, int)
{
    return juce::Font (sans (12.0f));
}

void HardwareLook::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    g.setFont (sans (12.0f));
    g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                  .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
    auto area = b.getLocalBounds().withTrimmedBottom (3);
    if (down)
        area = area.translated (0, 2);
    g.drawText (b.getButtonText().toUpperCase(), area, juce::Justification::centred);
}

void HardwareLook::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool down)
{
    const auto face = drawKey (g, b.getLocalBounds().toFloat(), over ? kKeyLight.brighter (0.3f) : kKeyLight, down);
    drawLed (g, { face.getX() + 13.0f, face.getCentreY() }, 3.5f, kOrange, b.getToggleState());
    g.setFont (sans (11.5f));
    g.setColour (kInk);
    g.drawText (b.getButtonText().toUpperCase(), face.withTrimmedLeft (24.0f).withTrimmedRight (6.0f),
                juce::Justification::centredLeft);
}

void HardwareLook::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                     float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);

    juce::Path track, value;
    track.addCentredArc (centre.x, centre.y, radius - 2.0f, radius - 2.0f, 0.0f, startAngle, endAngle, true);
    value.addCentredArc (centre.x, centre.y, radius - 2.0f, radius - 2.0f, 0.0f, startAngle, angle, true);
    g.setColour (kBodyLine);
    g.strokePath (track, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (kOrange);
    g.strokePath (value, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float capRadius = radius - 8.0f;
    const auto cap = juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre);
    const auto capColour = slider.findColour (juce::Slider::thumbColourId);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillEllipse (cap.translated (0.0f, 2.5f));
    g.setColour (capColour);
    g.fillEllipse (cap);
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.drawEllipse (cap.reduced (1.0f), 1.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -capRadius + 3.0f, 3.0f, capRadius * 0.55f, 1.5f);
    g.setColour (juce::Colours::white);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void HardwareLook::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                     float, float, juce::Slider::SliderStyle, juce::Slider& slider)
{
    // un fader: ranura oscura y una tapa clara
    const float alpha = slider.isEnabled() ? 1.0f : 0.4f;
    const auto slot = juce::Rectangle<float> ((float) x, (float) y + (float) height * 0.5f - 3.0f, (float) width, 6.0f);
    g.setColour (kKeyDark.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (slot, 3.0f);
    g.setColour (kOrange.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (slot.withRight (sliderPos), 3.0f);

    const auto cap = juce::Rectangle<float> (16.0f, std::min (24.0f, (float) height - 2.0f))
                         .withCentre ({ sliderPos, (float) y + (float) height * 0.5f });
    const auto face = drawKey (g, cap, kKeyLight.withMultipliedAlpha (alpha), false, 3.0f);
    g.setColour (kInk.withMultipliedAlpha (alpha));
    g.fillRect (juce::Rectangle<float> (2.0f, face.getHeight() - 8.0f).withCentre (face.getCentre()));
}

juce::Label* HardwareLook::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (mono (12.0f));
    return label;
}

void HardwareLook::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto face = drawKey (g, juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height), kKeyLight, false);
    juce::Path arrow;
    const float cx = face.getRight() - 14.0f, cy = face.getCentreY();
    arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
    arrow.lineTo (cx, cy + 2.5f);
    arrow.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (kInk.withAlpha (box.isEnabled() ? 1.0f : 0.4f));
    g.strokePath (arrow, juce::PathStrokeType (1.8f));
}

void HardwareLook::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 32, box.getHeight() - 5);
    label.setFont (getComboBoxFont (box));
}

juce::Font HardwareLook::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (sans (12.0f));
}

juce::Font HardwareLook::getPopupMenuFont()
{
    return juce::Font (sans (13.0f));
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
    g.fillAll (kDisplay);
    const auto src = proc.getSource();
    const float w = (float) getWidth(), h = (float) getHeight();
    const float flagH = 15.0f;
    const float mid = flagH + (h - flagH) * 0.5f;
    const float halfH = (h - flagH) * 0.5f - 2.0f;

    // linea central punteada, como una pantalla de segmentos
    g.setColour (kLcdDim.withAlpha (0.35f));
    for (float x = 0.0f; x < w; x += 6.0f)
        g.fillRect (x, mid, 2.0f, 1.0f);

    if (src == nullptr)
    {
        g.setColour (kOrange);
        g.setFont (mono (14.0f));
        g.drawText (proc.getStatusText().toUpperCase(), getLocalBounds(), juce::Justification::centred);
        return;
    }

    if (peaksFor != src.get() || (int) peaks.size() != getWidth())
        rebuildPeaks();

    const auto& slices = proc.getSlices();
    const int selected = proc.getSelectedPad();

    // zona del pad elegido
    if (slices[(size_t) selected].used)
    {
        juce::int64 end = src->buffer.getNumSamples();
        for (const auto& s : slices)
            if (s.used && s.start > slices[(size_t) selected].start)
                end = std::min (end, s.start);
        const float x0 = xForSample (slices[(size_t) selected].start);
        g.setColour (kOrange.withAlpha (0.22f));
        g.fillRect (x0, flagH, xForSample (end) - x0, h - flagH);
    }

    g.setColour (kLcd);
    for (int x = 0; x < (int) peaks.size(); ++x)
    {
        const float top = mid - peaks[(size_t) x].second * halfH;
        const float bottom = mid - peaks[(size_t) x].first * halfH;
        g.fillRect ((float) x, top, 1.0f, std::max (1.0f, bottom - top));
    }

    g.setFont (mono (10.0f));
    for (int pass = 0; pass < 2; ++pass)   // el elegido se dibuja al final, arriba de todo
        for (int i = 0; i < eng::kNumPads; ++i)
        {
            const bool isSelected = i == selected;
            if (! slices[(size_t) i].used || isSelected != (pass == 1))
                continue;
            const float x = xForSample (slices[(size_t) i].start);
            g.setColour (isSelected ? kOrange : kLcdDim);
            g.fillRect (x, 0.0f, isSelected ? 2.0f : 1.0f, h);
            const juce::Rectangle<float> flag (x, 0.0f, 17.0f, 13.0f);
            g.setColour (isSelected ? kOrange : kLcd);
            g.fillRoundedRectangle (flag, 2.0f);
            g.fillRect (flag.withWidth (3.0f));
            g.setColour (isSelected ? juce::Colours::white : kDisplay);
            g.drawText (juce::String (i + 1), flag, juce::Justification::centred);
        }

    g.setColour (kOrange.brighter (0.5f));
    for (int i = 0; i < eng::kNumPads; ++i)
    {
        const float pos = proc.engine.padPlayPos[(size_t) i].load (std::memory_order_relaxed);
        if (pos >= 0.0f && pos <= 1.0f)
            g.fillRect (pos * w, flagH, 1.5f, h - flagH);
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
        const bool used = slices[(size_t) i].used;
        const bool playing = proc.engine.padPlayPos[(size_t) i].load (std::memory_order_relaxed) >= 0.0f;
        const auto colour = playing ? kOrange : (used ? kKeyDark : kKeyGrey);
        const auto face = drawKey (g, padBounds (i).toFloat(), colour, held == i || playing, 7.0f);
        const auto inner = face.reduced (10.0f, 8.0f);

        g.setColour (juce::Colours::white.withAlpha (used ? 1.0f : 0.75f));
        g.setFont (sans (22.0f));
        g.drawText (juce::String (i + 1), inner, juce::Justification::topLeft);

        g.setFont (mono (11.0f));
        g.setColour (juce::Colours::white.withAlpha (playing ? 0.95f : 0.6f));
        if (i == learning)
            g.drawText ("TOCA UNA NOTA", inner, juce::Justification::bottomLeft);
        else if (used)
            g.drawText (noteName (proc.engine.padNote[(size_t) i].load()), inner, juce::Justification::bottomLeft);

        if (i == selected)
        {
            const auto ledColour = i == learning ? juce::Colours::yellow : (playing ? juce::Colours::white : kOrange);
            drawLed (g, { inner.getRight() - 3.0f, inner.getY() + 6.0f }, 4.0f, ledColour, true);
            g.setColour (ledColour);
            g.fillRoundedRectangle (face.getX() + 10.0f, face.getBottom() - 5.0f, face.getWidth() - 20.0f, 2.5f, 1.25f);
        }
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
    repaint();
}

void PadGrid::mouseUp (const juce::MouseEvent&)
{
    if (held >= 0)
        proc.engine.triggerPadFromUI (held, false);
    held = -1;
    repaint();
}

//==============================================================================
SamplerEditor::SamplerEditor (SamplerProcessor& p)
    : AudioProcessorEditor (&p), proc (p), waveform (p), padGrid (p)
{
    setLookAndFeel (&look);

    addAndMakeVisible (waveform);
    addAndMakeVisible (padGrid);

    for (auto* b : { &loadButton, &halfButton, &doubleButton, &sliceButton, &learnButton })
        addAndMakeVisible (*b);
    for (auto* b : { &syncButton, &stretchButton, &monoButton, &keysButton, &reverseButton, &gateButton })
        addAndMakeVisible (*b);
    addAndMakeVisible (sensSlider);
    addAndMakeVisible (modeBox);
    addAndMakeVisible (gridBox);

    setupCaption (bpmCaption, "BPM", juce::Justification::centredRight);
    setupCaption (tempoCaption, "TEMPO", juce::Justification::centredRight);
    setupCaption (modeCaption, "CORTAR POR", juce::Justification::centredLeft);
    setupCaption (gridCaption, "GRILLA", juce::Justification::centredLeft);
    setupCaption (sensCaption, "SENSIB.", juce::Justification::centredLeft);
    setupCaption (padTitle, "PAD", juce::Justification::centredLeft);
    setupCaption (globalTitle, "GLOBAL", juce::Justification::centredLeft);
    padTitle.setFont (sans (13.0f));
    padTitle.setColour (juce::Label::textColourId, kInk);
    globalTitle.setFont (sans (13.0f));
    globalTitle.setColour (juce::Label::textColourId, kInk);

    // textos de la pantalla
    for (auto* l : { &fileLabel, &statusLabel })
    {
        addAndMakeVisible (*l);
        l->setFont (mono (12.0f));
        l->setMinimumHorizontalScale (0.7f);
    }
    fileLabel.setColour (juce::Label::textColourId, kLcd);
    statusLabel.setColour (juce::Label::textColourId, kLcdDim);
    statusLabel.setJustificationType (juce::Justification::centredRight);

    setupLcd (bpmLabel);
    setupLcd (tempoLabel);
    bpmLabel.setEditable (true);
    bpmLabel.onTextChange = [this]
    {
        const double v = bpmLabel.getText().replaceCharacter (',', '.').getDoubleValue();
        if (v > 0.0)
            proc.setSampleBpm (v);
        refresh();
    };
    tempoLabel.onTextChange = [this]
    {
        const double v = tempoLabel.getText().replaceCharacter (',', '.').getDoubleValue();
        if (v > 0.0)
            proc.setManualBpm (v);
        refresh();
    };
    halfButton.onClick = [this] { proc.setSampleBpm (proc.getSampleBpm() * 0.5); };
    doubleButton.onClick = [this] { proc.setSampleBpm (proc.getSampleBpm() * 2.0); };
    for (auto* b : { &halfButton, &doubleButton })
        b->setColour (juce::TextButton::buttonColourId, kKeyGrey);
    sliceButton.setColour (juce::TextButton::buttonColourId, kOrange);

    sensSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    sensSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    sensSlider.setRange (0.0, 1.0, 0.01);
    sensSlider.setDoubleClickReturnValue (true, 0.5);
    sensSlider.setValue (proc.getSensitivity(), juce::dontSendNotification);
    sensSlider.onValueChange = [this] { proc.setSensitivity (sensSlider.getValue()); };

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
    master.slider.setColour (juce::Slider::thumbColourId, kOrange);
    padGain.slider.setColour (juce::Slider::thumbColourId, kKeyGrey);

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

    sendLookAndFeelChange();   // los controles ya creados toman el estilo nuevo

    proc.addChangeListener (this);
    refresh();
    startTimerHz (30);
    setSize (1000, 720);
}

SamplerEditor::~SamplerEditor()
{
    proc.removeChangeListener (this);
    proc.engine.learnPad.store (-1);
    setLookAndFeel (nullptr);
}

void SamplerEditor::setupCaption (juce::Label& l, const juce::String& text, juce::Justification just)
{
    addAndMakeVisible (l);
    l.setText (text, juce::dontSendNotification);
    l.setFont (sans (10.0f));
    l.setColour (juce::Label::textColourId, kInkDim);
    l.setJustificationType (just);
}

void SamplerEditor::setupLcd (juce::Label& l)
{
    addAndMakeVisible (l);
    l.setFont (mono (14.0f));
    l.setColour (juce::Label::textColourId, kOrange);
    l.setJustificationType (juce::Justification::centred);
}

void SamplerEditor::setupKnob (Knob& k, const juce::String& name, const juce::String& suffix)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
    k.slider.setTextValueSuffix (suffix);
    addAndMakeVisible (k.slider);
    setupCaption (k.label, name.toUpperCase(), juce::Justification::centred);
}

void SamplerEditor::layoutKnob (Knob& k, juce::Rectangle<int> area)
{
    k.label.setBounds (area.removeFromTop (14));
    k.slider.setBounds (area.reduced (4, 0));
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

    fileLabel.setText (proc.getFileName().isEmpty() ? juce::String ("SIN AUDIO") : proc.getFileName(),
                       juce::dontSendNotification);
    if (! bpmLabel.isBeingEdited())
        bpmLabel.setText (juce::String (proc.getSampleBpm(), 2), juce::dontSendNotification);
    statusLabel.setText (proc.getSource() != nullptr ? proc.getStatusText().toUpperCase() : juce::String(),
                         juce::dontSendNotification);
    padTitle.setText ("PAD " + juce::String (pad + 1).paddedLeft ('0', 2) + "  /  NOTA "
                          + noteName (proc.engine.padNote[(size_t) pad].load()),
                      juce::dontSendNotification);
    modeBox.setSelectedItemIndex ((int) proc.getSliceMode(), juce::dontSendNotification);
    gridBox.setSelectedItemIndex (proc.getGridIndex(), juce::dontSendNotification);
    gridBox.setEnabled (proc.getSliceMode() == eng::SliceMode::grid);
    sensSlider.setValue (proc.getSensitivity(), juce::dontSendNotification);
    sensSlider.setEnabled (proc.getSliceMode() == eng::SliceMode::transients);

    // el tempo destino: el del proyecto si llega; si no, se escribe a mano
    const bool hostTempo = proc.hasHostTempo();
    tempoCaption.setText (hostTempo ? "PROYECTO" : "TEMPO", juce::dontSendNotification);
    if (tempoLabel.isEditable() == hostTempo)
        tempoLabel.setEditable (! hostTempo);
    if (! tempoLabel.isBeingEdited())
        tempoLabel.setText (juce::String (proc.getTargetBpm(), 1), juce::dontSendNotification);

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

    const bool hostTempo = proc.hasHostTempo();
    if (tempoLabel.isEditable() == hostTempo)
        refresh();
    else if (hostTempo && ! tempoLabel.isBeingEdited())
        tempoLabel.setText (juce::String (proc.getTargetBpm(), 1), juce::dontSendNotification);

    statusLabel.setText (proc.getSource() != nullptr ? proc.getStatusText().toUpperCase() : juce::String(),
                         juce::dontSendNotification);
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
    g.fillAll (kBody);

    // marca
    auto logo = logoBounds;
    g.setColour (kInk);
    g.setFont (sans (27.0f).withKerningFactor (-0.02f));
    g.drawText (JucePlugin_Name, logo.removeFromLeft (128), juce::Justification::centredLeft);
    g.setColour (kOrange);
    g.fillRect (logo.removeFromLeft (4).reduced (0, 9));
    logo.removeFromLeft (7);
    g.setColour (kInkDim);
    g.setFont (mono (9.0f));
    g.drawFittedText ("16 PAD\nSLICE SAMPLER", logo, juce::Justification::centredLeft, 2);

    // pantalla
    g.setColour (kDisplay);
    g.fillRoundedRectangle (displayBounds.toFloat(), 9.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (displayBounds.toFloat().reduced (3.0f), 6.5f, 1.0f);

    // mini pantallas de tempo
    g.setColour (kDisplay);
    g.fillRoundedRectangle (bpmLabel.getBounds().toFloat(), 4.0f);
    g.fillRoundedRectangle (tempoLabel.getBounds().toFloat(), 4.0f);

    // lineas de seccion
    g.setColour (kBodyLine);
    for (auto* title : { &padTitle, &globalTitle })
        g.fillRect (title->getX(), title->getBottom() + 1, getWidth() - 18 - title->getX(), 1);
    g.fillRect (18, padGrid.getY() - 9, getWidth() - 36, 1);
}

void SamplerEditor::paintOverChildren (juce::Graphics& g)
{
    if (dragOver)
    {
        g.setColour (kOrange);
        g.drawRoundedRectangle (displayBounds.toFloat().reduced (1.5f), 9.0f, 3.0f);
    }
}

void SamplerEditor::resized()
{
    auto area = getLocalBounds().reduced (18, 14);

    // cabecera
    auto top = area.removeFromTop (36);
    logoBounds = top.removeFromLeft (232);
    loadButton.setBounds (top.removeFromLeft (92).withSizeKeepingCentre (92, 32));
    stretchButton.setBounds (top.removeFromRight (100).withSizeKeepingCentre (100, 32));
    top.removeFromRight (6);
    syncButton.setBounds (top.removeFromRight (80).withSizeKeepingCentre (80, 32));
    top.removeFromRight (14);
    tempoLabel.setBounds (top.removeFromRight (62).withSizeKeepingCentre (62, 26));
    top.removeFromRight (4);
    tempoCaption.setBounds (top.removeFromRight (64));
    top.removeFromRight (10);
    doubleButton.setBounds (top.removeFromRight (36).withSizeKeepingCentre (36, 32));
    top.removeFromRight (4);
    halfButton.setBounds (top.removeFromRight (36).withSizeKeepingCentre (36, 32));
    top.removeFromRight (6);
    bpmLabel.setBounds (top.removeFromRight (70).withSizeKeepingCentre (70, 26));
    top.removeFromRight (4);
    bpmCaption.setBounds (top.removeFromRight (34));

    // pantalla
    area.removeFromTop (10);
    displayBounds = area.removeFromTop (214);
    auto screen = displayBounds.reduced (12, 9);
    auto info = screen.removeFromTop (18);
    statusLabel.setBounds (info.removeFromRight (info.getWidth() / 2));
    fileLabel.setBounds (info);
    screen.removeFromTop (4);
    waveform.setBounds (screen);

    // fila de cortes
    area.removeFromTop (10);
    auto sliceRow = area.removeFromTop (32);
    modeCaption.setBounds (sliceRow.removeFromLeft (84));
    modeBox.setBounds (sliceRow.removeFromLeft (160));
    sliceRow.removeFromLeft (18);
    gridCaption.setBounds (sliceRow.removeFromLeft (54));
    gridBox.setBounds (sliceRow.removeFromLeft (150));
    sliceRow.removeFromLeft (18);
    sensCaption.setBounds (sliceRow.removeFromLeft (58));
    sensSlider.setBounds (sliceRow.removeFromLeft (150));
    sliceButton.setBounds (sliceRow.removeFromRight (120));

    // pads a la izquierda, controles a la derecha
    area.removeFromTop (18);
    padGrid.setBounds (area.removeFromLeft (400).expanded (4, 4).withTrimmedBottom (-2));
    area.removeFromLeft (26);

    padTitle.setBounds (area.removeFromTop (20));
    area.removeFromTop (8);
    auto row1 = area.removeFromTop (102);
    const int kw = row1.getWidth() / 5;
    layoutKnob (padGain, row1.removeFromLeft (kw));
    layoutKnob (padPan, row1.removeFromLeft (kw));
    layoutKnob (padPitch, row1.removeFromLeft (kw));
    layoutKnob (padAttack, row1.removeFromLeft (kw));
    layoutKnob (padRelease, row1);

    area.removeFromTop (8);
    auto row2 = area.removeFromTop (32);
    reverseButton.setBounds (row2.removeFromLeft (106));
    row2.removeFromLeft (8);
    gateButton.setBounds (row2.removeFromLeft (114));
    learnButton.setBounds (row2.removeFromRight (140));

    area.removeFromTop (18);
    globalTitle.setBounds (area.removeFromTop (20));
    area.removeFromTop (8);
    auto row3 = area.removeFromTop (102);
    layoutKnob (master, row3.removeFromLeft (kw));
    layoutKnob (vel, row3.removeFromLeft (kw));
    layoutKnob (tune, row3.removeFromLeft (kw));
    layoutKnob (root, row3.removeFromLeft (kw));

    area.removeFromTop (8);
    auto row4 = area.removeFromTop (32);
    monoButton.setBounds (row4.removeFromLeft (106));
    row4.removeFromLeft (8);
    keysButton.setBounds (row4.removeFromLeft (114));
}

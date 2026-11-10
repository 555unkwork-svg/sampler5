#include "PluginEditor.h"
#include "BinaryData.h"

using namespace ui;

namespace
{
void buildPeaks (std::vector<std::pair<float, float>>& peaks, const juce::AudioBuffer<float>& buffer,
                 int width, double start, double length)
{
    peaks.assign ((size_t) std::max (0, width), { 0.0f, 0.0f });
    const int n = buffer.getNumSamples();
    for (int x = 0; x < width; ++x)
    {
        const int from = juce::jlimit (0, n, (int) (start + length * x / width));
        const int to = juce::jlimit (from, n, std::max (from + 1, (int) (start + length * (x + 1) / width)));
        float lo = 0.0f, hi = 0.0f;
        for (int c = 0; c < buffer.getNumChannels() && to > from; ++c)
        {
            const auto range = buffer.findMinMax (c, from, to - from);
            lo = std::min (lo, range.getStart());
            hi = std::max (hi, range.getEnd());
        }
        peaks[(size_t) x] = { lo, hi };
    }
}

void drawStar (juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour)
{
    juce::Path star;
    star.addStar (centre, 5, radius * 0.45f, radius);
    g.setColour (colour);
    g.fillPath (star);
}
}

//==============================================================================
WaveformView::WaveformView (SamplerProcessor& p) : proc (p) {}

double WaveformView::totalLength() const
{
    const auto src = proc.getSource();
    return src != nullptr ? (double) src->buffer.getNumSamples() : 0.0;
}

juce::Range<double> WaveformView::getVisibleRange() const
{
    const double total = totalLength();
    if (mode == Mode::source || viewLength <= 0.0 || viewLength >= total)
        return { 0.0, total };
    return { viewStart, viewStart + viewLength };
}

void WaveformView::setMode (Mode m)
{
    mode = m;
    drag = Drag::none;
    repaint();
}

void WaveformView::zoom (double factor, double anchor)
{
    const double total = totalLength();
    if (total <= 0.0 || mode == Mode::source)
        return;
    const auto visible = getVisibleRange();
    const double minLength = std::min (total, 512.0);
    const double newLength = juce::jlimit (minLength, total, visible.getLength() / factor);
    const double pivot = visible.getStart() + visible.getLength() * anchor;
    viewLength = newLength >= total ? 0.0 : newLength;
    viewStart = juce::jlimit (0.0, total - newLength, pivot - newLength * anchor);
    repaint();
}

void WaveformView::scroll (double fractionOfView)
{
    const double total = totalLength();
    const auto visible = getVisibleRange();
    if (visible.getLength() >= total)
        return;
    viewStart = juce::jlimit (0.0, total - visible.getLength(), visible.getStart() + visible.getLength() * fractionOfView);
    repaint();
}

void WaveformView::centreOn (juce::int64 sample)
{
    const double total = totalLength();
    const auto visible = getVisibleRange();
    if (visible.getLength() >= total)
        return;
    viewStart = juce::jlimit (0.0, total - visible.getLength(), (double) sample - visible.getLength() * 0.5);
    repaint();
}

float WaveformView::xForSample (double sample) const
{
    const auto visible = getVisibleRange();
    if (visible.getLength() <= 0.0)
        return 0.0f;
    return (float) ((sample - visible.getStart()) / visible.getLength() * getWidth());
}

juce::int64 WaveformView::sampleForX (float x) const
{
    const auto visible = getVisibleRange();
    if (getWidth() <= 0)
        return 0;
    const double s = visible.getStart() + juce::jlimit (0.0, 1.0, (double) x / getWidth()) * visible.getLength();
    return (juce::int64) juce::jlimit (0.0, std::max (0.0, totalLength() - 1.0), s);
}

juce::int64 WaveformView::snapTolerance() const
{
    // el iman actua a menos de 10 pixeles
    return (juce::int64) std::max (1.0, getVisibleRange().getLength() / std::max (1, getWidth()) * 10.0);
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
        const float d = std::abs (xForSample ((double) slices[(size_t) i].start) - x);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

bool WaveformView::overSelectedEnd (float x) const
{
    const int selected = proc.getSelectedPad();
    if (! proc.getSlices()[(size_t) selected].used)
        return false;
    return std::abs (xForSample ((double) proc.getSliceEnd (selected)) - x) < 6.0f;
}

void WaveformView::checkSource()
{
    const auto src = proc.getSource();
    if (viewFor != src.get())
    {
        // audio nuevo: volvemos a ver todo
        viewFor = src.get();
        viewStart = 0.0;
        viewLength = 0.0;
    }
}

void WaveformView::rebuildPeaks()
{
    const auto src = proc.getSource();
    const auto visible = getVisibleRange();
    peaksFor = src.get();
    peaksStart = visible.getStart();
    peaksLength = visible.getLength();
    if (src != nullptr)
        buildPeaks (peaks, src->buffer, getWidth(), peaksStart, peaksLength);
}

void WaveformView::paint (juce::Graphics& g)
{
    checkSource();
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
        g.setColour (kRed);
        g.setFont (mono (14.0f));
        g.drawText (proc.getStatusText().toUpperCase(), getLocalBounds(), juce::Justification::centred);
        return;
    }

    const auto visible = getVisibleRange();
    if (peaksFor != src.get() || (int) peaks.size() != getWidth()
        || peaksStart != visible.getStart() || peaksLength != visible.getLength())
        rebuildPeaks();

    g.setColour (mode == Mode::analysis ? kLcd.withAlpha (0.55f) : kLcd);
    for (int x = 0; x < (int) peaks.size(); ++x)
    {
        const float top = mid - peaks[(size_t) x].second * halfH;
        const float bottom = mid - peaks[(size_t) x].first * halfH;
        g.fillRect ((float) x, top, 1.0f, std::max (1.0f, bottom - top));
    }

    switch (mode)
    {
        case Mode::source:   paintSlices (g, false); paintSource (g); break;
        case Mode::slices:   paintSlices (g, true); break;
        case Mode::analysis: paintAnalysis (g); break;
    }

    g.setColour (kRed.brighter (0.5f));
    for (int i = 0; i < eng::kNumPads; ++i)
    {
        const float pos = proc.engine.padPlayPos[(size_t) i].load (std::memory_order_relaxed);
        if (pos >= 0.0f && pos <= 1.0f)
            g.fillRect (xForSample ((double) pos * totalLength()), flagH, 1.5f, h - flagH);
    }
}

void WaveformView::paintSlices (juce::Graphics& g, bool editable)
{
    const auto& slices = proc.getSlices();
    const int selected = proc.getSelectedPad();
    const float h = (float) getHeight();
    const float flagH = 15.0f;
    const float dim = editable ? 1.0f : 0.45f;

    // zona del pad elegido, con su fin
    if (editable && slices[(size_t) selected].used)
    {
        const float x0 = xForSample ((double) slices[(size_t) selected].start);
        const float x1 = xForSample ((double) proc.getSliceEnd (selected));
        g.setColour (kRed.withAlpha (0.22f));
        g.fillRect (x0, flagH, x1 - x0, h - flagH);

        // manija del fin: una linea con un triangulo abajo
        g.setColour (kRed);
        g.fillRect (x1 - 1.0f, flagH, 1.0f, h - flagH);
        juce::Path handle;
        handle.addTriangle (x1, h, x1 - 8.0f, h, x1, h - 8.0f);
        g.fillPath (handle);
    }

    g.setFont (mono (10.0f));
    for (int pass = 0; pass < 2; ++pass)   // el elegido se dibuja al final, arriba de todo
        for (int i = 0; i < eng::kNumPads; ++i)
        {
            const bool isSelected = editable && i == selected;
            const auto& s = slices[(size_t) i];
            if (! s.used || isSelected != (pass == 1))
                continue;
            const float x = xForSample ((double) s.start);
            if (x < -20.0f || x > (float) getWidth() + 2.0f)
                continue;
            g.setColour ((isSelected ? kRed : kLcdDim).withMultipliedAlpha (dim));
            g.fillRect (x, 0.0f, isSelected ? 2.0f : 1.0f, h);
            const juce::Rectangle<float> flag (x, 0.0f, 17.0f, 13.0f);
            g.setColour ((isSelected ? kRed : kLcd).withMultipliedAlpha (dim));
            g.fillRoundedRectangle (flag, 2.0f);
            g.fillRect (flag.withWidth (3.0f));
            g.setColour (isSelected ? juce::Colours::white : kDisplay);
            g.drawText (juce::String (i + 1), flag, juce::Justification::centred);
            if (s.favorite)
                drawStar (g, { x + 24.0f, 6.5f }, 5.5f, kRed.withMultipliedAlpha (dim));
        }
}

void WaveformView::paintSource (juce::Graphics& g)
{
    // lo que queda fuera de la zona se oscurece
    const float w = (float) getWidth(), h = (float) getHeight();
    const float x0 = xForSample ((double) proc.getRegionStart());
    const float x1 = xForSample ((double) proc.getRegionEnd());
    g.setColour (kDisplay.withAlpha (0.72f));
    g.fillRect (0.0f, 0.0f, x0, h);
    g.fillRect (x1, 0.0f, w - x1, h);

    g.setColour (kRed);
    for (float x : { x0, x1 })
    {
        g.fillRect (x - 1.0f, 0.0f, 2.0f, h);
        g.fillRoundedRectangle (juce::Rectangle<float> (10.0f, 26.0f).withCentre ({ x, h * 0.5f }), 3.0f);
    }

    g.setFont (mono (11.0f));
    g.setColour (kLcd);
    g.drawText ("ZONA A USAR: ARRASTRA LOS BORDES ROJOS", getLocalBounds().reduced (8, 3),
                juce::Justification::bottomLeft);
}

void WaveformView::paintAnalysis (juce::Graphics& g)
{
    const auto& analysis = proc.getAnalysis();
    const float w = (float) getWidth(), h = (float) getHeight();
    const float base = h - 2.0f, range = h - 22.0f;

    // el umbral que deja pasar la sensibilidad
    const double s = proc.getSensitivity();
    const float threshold = (float) (0.6 * (1.0 - s) * (1.0 - s));
    const float ty = base - threshold * range;
    g.setColour (kLcdDim);
    for (float x = 0.0f; x < w; x += 8.0f)
        g.fillRect (x, ty, 4.0f, 1.0f);

    int inView = 0;
    for (const auto& o : analysis.onsets)
    {
        const float x = xForSample ((double) o.position);
        if (x < 0.0f || x > w)
            continue;
        ++inView;
        const bool passes = o.strength >= threshold;
        g.setColour (passes ? kRed : kLcdDim);
        g.fillRect (x - 1.0f, base - o.strength * range, 2.0f, o.strength * range);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ x, base - o.strength * range }));
    }

    g.setFont (mono (11.0f));
    g.setColour (kLcd);
    g.drawText ("GOLPES DETECTADOS: " + juce::String ((int) analysis.onsets.size())
                    + "   EN ROJO: LOS QUE ENTRAN CON LA SENSIBILIDAD ACTUAL",
                getLocalBounds().reduced (8, 2), juce::Justification::topLeft);
    juce::ignoreUnused (inView);
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    bool resize = false;
    if (mode == Mode::slices)
        resize = markerAt ((float) e.x) >= 0 || overSelectedEnd ((float) e.x);
    else if (mode == Mode::source)
        resize = std::abs (xForSample ((double) proc.getRegionStart()) - (float) e.x) < 8.0f
              || std::abs (xForSample ((double) proc.getRegionEnd()) - (float) e.x) < 8.0f;
    setMouseCursor (resize ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    drag = Drag::none;
    if (proc.getSource() == nullptr)
        return;
    const float x = (float) e.x;

    if (mode == Mode::source)
    {
        const float d0 = std::abs (xForSample ((double) proc.getRegionStart()) - x);
        const float d1 = std::abs (xForSample ((double) proc.getRegionEnd()) - x);
        if (std::min (d0, d1) < 10.0f)
            drag = d0 < d1 ? Drag::regionStart : Drag::regionEnd;
        return;
    }

    if (mode != Mode::slices)
        return;

    const int marker = markerAt (x);
    if (e.mods.isPopupMenu())
    {
        if (marker >= 0)
        {
            proc.setSelectedPad (marker);
            if (onSliceMenu != nullptr)
                onSliceMenu (marker);
        }
        return;
    }

    // el inicio de un corte gana sobre el fin del elegido si estan en el mismo lugar
    if (marker >= 0)
    {
        drag = Drag::sliceStart;
        dragPad = marker;
        proc.setSelectedPad (marker);
    }
    else if (overSelectedEnd (x))
    {
        drag = Drag::sliceEnd;
        dragPad = proc.getSelectedPad();
    }
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    const auto position = sampleForX ((float) e.x);
    switch (drag)
    {
        case Drag::sliceStart:
            proc.setSliceStart (dragPad, proc.snapPosition (position, snapTolerance()));
            break;
        case Drag::sliceEnd:
            proc.setSliceEnd (dragPad, proc.snapPosition (position, snapTolerance()));
            break;
        case Drag::regionStart:
            proc.setRegion (position, proc.getRegionEnd());
            break;
        case Drag::regionEnd:
            proc.setRegion (proc.getRegionStart(), position);
            break;
        case Drag::none:
            break;
    }
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    drag = Drag::none;
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (mode != Mode::slices || proc.getSource() == nullptr || markerAt ((float) e.x) >= 0)
        return;
    const int pad = proc.addSlice (proc.snapPosition (sampleForX ((float) e.x), snapTolerance()));
    if (pad >= 0)
        proc.setSelectedPad (pad);
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (mode == Mode::source)
        return;
    if (std::abs (wheel.deltaX) > std::abs (wheel.deltaY) || e.mods.isShiftDown())
        scroll (-(std::abs (wheel.deltaX) > 0.0f ? wheel.deltaX : wheel.deltaY) * 0.5);
    else if (wheel.deltaY != 0.0f)
        zoom (std::pow (2.0, (double) wheel.deltaY * 2.0), (double) e.x / std::max (1, getWidth()));
}

//==============================================================================
OverviewStrip::OverviewStrip (SamplerProcessor& p, WaveformView& w) : proc (p), waveform (w) {}

void OverviewStrip::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (kDisplay);
    g.fillRoundedRectangle (bounds, 5.0f);

    const auto src = proc.getSource();
    if (src == nullptr)
        return;

    const auto inner = getLocalBounds().reduced (4, 3);
    if (peaksFor != src.get() || (int) peaks.size() != inner.getWidth())
    {
        peaksFor = src.get();
        buildPeaks (peaks, src->buffer, inner.getWidth(), 0.0, (double) src->buffer.getNumSamples());
    }

    const double total = (double) src->buffer.getNumSamples();
    const float mid = (float) inner.getCentreY(), halfH = (float) inner.getHeight() * 0.5f;
    g.setColour (kLcdDim);
    for (int x = 0; x < (int) peaks.size(); ++x)
    {
        const float top = mid - peaks[(size_t) x].second * halfH;
        const float bottom = mid - peaks[(size_t) x].first * halfH;
        g.fillRect ((float) (inner.getX() + x), top, 1.0f, std::max (1.0f, bottom - top));
    }

    g.setColour (kRed.withAlpha (0.8f));
    for (const auto& s : proc.getSlices())
        if (s.used)
            g.fillRect ((float) inner.getX() + (float) ((double) s.start / total * inner.getWidth()),
                        (float) inner.getY(), 1.0f, (float) inner.getHeight());

    // la parte que se ve en la pantalla
    const auto visible = waveform.getVisibleRange();
    const float x0 = (float) inner.getX() + (float) (visible.getStart() / total * inner.getWidth());
    const float x1 = (float) inner.getX() + (float) (visible.getEnd() / total * inner.getWidth());
    const juce::Rectangle<float> window (x0, (float) inner.getY(), std::max (3.0f, x1 - x0), (float) inner.getHeight());
    g.setColour (kLcd.withAlpha (0.16f));
    g.fillRect (window);
    g.setColour (kLcd);
    g.drawRect (window, 1.0f);
}

void OverviewStrip::mouseDrag (const juce::MouseEvent& e)
{
    const auto src = proc.getSource();
    const auto inner = getLocalBounds().reduced (4, 3);
    if (src == nullptr || inner.getWidth() <= 0)
        return;
    const double t = juce::jlimit (0.0, 1.0, (double) (e.x - inner.getX()) / inner.getWidth());
    waveform.centreOn ((juce::int64) (t * src->buffer.getNumSamples()));
    repaint();
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
        const auto& slice = slices[(size_t) i];
        const bool used = slice.used;
        const bool playing = proc.engine.padPlayPos[(size_t) i].load (std::memory_order_relaxed) >= 0.0f;
        const auto colour = playing ? kRed : (used ? kKeyDark : kKeyGrey);
        const auto face = drawKey (g, padBounds (i).toFloat(), colour, held == i || playing, 7.0f);
        const auto inner = face.reduced (9.0f, 6.0f);

        g.setColour (juce::Colours::white.withAlpha (used ? 1.0f : 0.75f));
        g.setFont (sans (20.0f));
        g.drawText (juce::String (i + 1), inner, juce::Justification::topLeft);

        g.setFont (mono (10.0f));
        g.setColour (juce::Colours::white.withAlpha (playing ? 0.95f : 0.6f));
        if (i == learning)
        {
            g.drawText ("TOCA UNA NOTA", inner, juce::Justification::bottomLeft);
        }
        else if (used)
        {
            auto text = inner;
            g.drawText (noteName (proc.engine.padNote[(size_t) i].load()), text.removeFromBottom (12.0f),
                        juce::Justification::centredLeft);
            g.drawText (proc.getSliceName (i).toUpperCase(), text.removeFromBottom (12.0f),
                        juce::Justification::centredLeft, true);
        }
        else
        {
            g.drawText ("VACIO", inner, juce::Justification::bottomLeft);
        }

        float right = inner.getRight() - 3.0f;
        if (i == selected)
        {
            const auto ledColour = i == learning ? juce::Colours::yellow : (playing ? juce::Colours::white : kRed);
            drawLed (g, { right, inner.getY() + 6.0f }, 4.0f, ledColour, true);
            g.setColour (ledColour);
            g.fillRoundedRectangle (face.getX() + 10.0f, face.getBottom() - 4.0f, face.getWidth() - 20.0f, 2.0f, 1.0f);
            right -= 16.0f;
        }
        if (used && slice.favorite)
            drawStar (g, { right, inner.getY() + 6.0f }, 6.0f, playing ? juce::Colours::white : kRed);
    }
}

void PadGrid::mouseDown (const juce::MouseEvent& e)
{
    const int pad = padAt (e.getPosition());
    if (pad < 0)
        return;
    proc.setSelectedPad (pad);

    if (e.mods.isPopupMenu())
    {
        if (onSliceMenu != nullptr)
            onSliceMenu (pad);
        return;
    }

    held = pad;
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
    : AudioProcessorEditor (&p), proc (p), waveform (p), overview (p, waveform), padGrid (p)
{
    setLookAndFeel (&look);
   #if SAMPLER_HAS_CREST
    crest = juce::ImageCache::getFromMemory (BinaryData::escudo_png, BinaryData::escudo_pngSize);
   #endif

    addAndMakeVisible (waveform);
    addAndMakeVisible (overview);
    addAndMakeVisible (padGrid);
    waveform.onSliceMenu = [this] (int pad) { showSliceMenu (pad); };
    padGrid.onSliceMenu = [this] (int pad) { showSliceMenu (pad); };

    for (auto* b : { &detectButton, &halfButton, &doubleButton, &sourceTab, &slicesTab, &analysisTab,
                     &resliceButton, &loadButton, &navLeftButton, &navRightButton, &zoomOutButton, &zoomInButton,
                     &silenceButton })
        addAndMakeVisible (*b);
    for (auto* b : { &syncButton, &stretchButton, &snapButton, &invButton })
        addAndMakeVisible (*b);
    for (auto* c : { &modeBox, &gridBox, &snapBox })
        addAndMakeVisible (*c);
    addAndMakeVisible (sensSlider);

    //---- cabecera
    setupCaption (bpmCaption, "BPM", juce::Justification::centredRight);
    setupCaption (tempoCaption, "TEMPO", juce::Justification::centredRight);
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
    detectButton.onClick = [this] { proc.detectBpm(); };
    halfButton.onClick = [this] { proc.setSampleBpm (proc.getSampleBpm() * 0.5); };
    doubleButton.onClick = [this] { proc.setSampleBpm (proc.getSampleBpm() * 2.0); };
    for (auto* b : { &halfButton, &doubleButton, &detectButton, &navLeftButton, &navRightButton,
                     &zoomOutButton, &zoomInButton })
        b->setColour (juce::TextButton::buttonColourId, kInkDim);

    //---- vistas de la pantalla
    sourceTab.onClick = [this] { setViewMode (WaveformView::Mode::source); };
    slicesTab.onClick = [this] { setViewMode (WaveformView::Mode::slices); };
    analysisTab.onClick = [this] { setViewMode (WaveformView::Mode::analysis); };
    resliceButton.setColour (juce::TextButton::buttonColourId, kRed);

    snapBox.addItemList ({ "Auto", "Golpe", "Cruce por cero", "Grilla" }, 1);
    snapButton.onClick = [this]
    {
        proc.setSnap (snapButton.getToggleState(), (SamplerProcessor::SnapMode) snapBox.getSelectedItemIndex());
    };
    snapBox.onChange = snapButton.onClick;

    setupCaption (infoLabel, "", juce::Justification::centred);
    infoLabel.setFont (mono (12.0f));
    infoLabel.setColour (juce::Label::textColourId, kInk);
    for (auto* l : { &fileLabel, &statusLabel })
    {
        addAndMakeVisible (*l);
        l->setFont (mono (12.0f));
        l->setMinimumHorizontalScale (0.7f);
    }
    fileLabel.setColour (juce::Label::textColourId, kLcd);
    statusLabel.setColour (juce::Label::textColourId, kLcdDim);
    statusLabel.setJustificationType (juce::Justification::centredRight);

    //---- fila de cortes
    setupCaption (modeCaption, "CORTAR POR", juce::Justification::centredLeft);
    setupCaption (gridCaption, "GRILLA", juce::Justification::centredLeft);
    setupCaption (sensCaption, "SENSIB.", juce::Justification::centredLeft);
    sensSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    sensSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    sensSlider.setRange (0.0, 1.0, 0.01);
    sensSlider.setDoubleClickReturnValue (true, 0.5);
    sensSlider.onValueChange = [this] { proc.setSensitivity (sensSlider.getValue()); };

    modeBox.addItemList ({ "Golpes", "Grilla", "Partes iguales", "Al azar", "A mano (vacio)" }, 1);
    gridBox.addItemList (SamplerProcessor::gridNames(), 1);
    auto reslice = [this]
    {
        proc.autoSlice ((eng::SliceMode) modeBox.getSelectedItemIndex(), gridBox.getSelectedItemIndex());
    };
    modeBox.onChange = reslice;
    gridBox.onChange = [this, reslice] { if (modeBox.getSelectedItemIndex() == (int) eng::SliceMode::grid) reslice(); };
    resliceButton.onClick = reslice;

    //---- seccion MUESTRA
    setupCaption (sampleCaption, "MUESTRA", juce::Justification::centredLeft);
    sampleCaption.setFont (sans (13.0f));
    sampleCaption.setColour (juce::Label::textColourId, kInk);
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
    silenceButton.onClick = [this] { proc.engine.allNotesOff(); };
    navLeftButton.onClick = [this] { waveform.scroll (-0.5); };
    navRightButton.onClick = [this] { waveform.scroll (0.5); };
    zoomOutButton.onClick = [this] { waveform.zoom (0.5); };
    zoomInButton.onClick = [this] { waveform.zoom (2.0); };

    //---- panel derecho: pestanas
    const char* tabNames[numTabs] = { "Pad", "Filtro", "Global", "Avanzado", "MIDI" };
    for (int i = 0; i < numTabs; ++i)
    {
        addAndMakeVisible (panelTabs[i]);
        panelTabs[i].setButtonText (tabNames[i]);
        panelTabs[i].onClick = [this, i] { setPanelTab (i); };
    }

    //---- pestana PAD
    setupCaption (padTitle, "PAD", juce::Justification::centredLeft, tabPad);
    padTitle.setFont (sans (14.0f));
    padTitle.setColour (juce::Label::textColourId, kInk);
    setupCaption (nameLabel, "", juce::Justification::centredLeft, tabPad);
    nameLabel.setFont (sans (14.0f));
    nameLabel.setColour (juce::Label::textColourId, kRed);
    nameLabel.setEditable (false, true);
    nameLabel.onTextChange = [this] { proc.setSliceName (proc.getSelectedPad(), nameLabel.getText()); };
    setupCaption (noteLabel, "", juce::Justification::centredRight, tabPad);
    noteLabel.setFont (sans (12.0f));

    addToTab (tabPad, favButton);
    favButton.setColour (juce::TextButton::buttonColourId, kInkDim);
    favButton.onClick = [this]
    {
        const int pad = proc.getSelectedPad();
        proc.setSliceFavorite (pad, ! proc.getSlices()[(size_t) pad].favorite);
    };

    setupKnob (padGain, "Vol", " dB", tabPad);
    setupKnob (padPan, "Pan", "", tabPad);
    setupKnob (padPitch, "Tono", " st", tabPad);
    setupKnob (padFine, "Fino", " ct", tabPad);
    setupKnob (padVelAmt, "Velocidad", " %", tabPad);
    setupKnob (padPreGain, "Ganancia", " dB", tabPad);
    setupKnob (padAttack, "Ataque", " ms", tabPad);
    setupKnob (padRelease, "Liberacion", " ms", tabPad);

    setupCaption (startCaption, "INICIO", juce::Justification::centredLeft, tabPad);
    setupCaption (endCaption, "FINAL", juce::Justification::centredLeft, tabPad);
    setupPercentSlider (startSlider);
    setupPercentSlider (endSlider);
    auto percentToSample = [this] (double percent)
    {
        const auto src = proc.getSource();
        return src != nullptr ? (juce::int64) (percent * 0.01 * src->buffer.getNumSamples()) : (juce::int64) 0;
    };
    startSlider.onValueChange = [this, percentToSample]
    {
        proc.setSliceStart (proc.getSelectedPad(), percentToSample (startSlider.getValue()));
    };
    endSlider.onValueChange = [this, percentToSample]
    {
        proc.setSliceEnd (proc.getSelectedPad(), percentToSample (endSlider.getValue()));
    };

    addToTab (tabPad, reverseButton);
    addToTab (tabPad, loopButton);
    addToTab (tabPad, muteButton);
    setupCaption (playCaption, "REPRODUCCION", juce::Justification::centredLeft, tabPad);

    //---- pestana FILTRO (del pad elegido)
    setupCaption (filterTitle, "FILTRO", juce::Justification::centredLeft, tabFilter);
    filterTitle.setFont (sans (13.0f));
    filterTitle.setColour (juce::Label::textColourId, kInk);
    setupCaption (filterCaption, "TIPO", juce::Justification::centredLeft, tabFilter);
    addToTab (tabFilter, filterBox);
    filterBox.addItemList ({ "Apagado", "Pasa bajos", "Pasa altos" }, 1);
    setupKnob (padCutoff, "Corte", " Hz", tabFilter);
    setupKnob (padReso, "Resonancia", " %", tabFilter);
    addToTab (tabPad, holdButton);
    addToTab (tabPad, shotButton);
    holdButton.onClick = [this] { setPadGate (true); };
    shotButton.onClick = [this] { setPadGate (false); };

    //---- pestana GLOBAL
    setupCaption (globalTitle, "GLOBAL", juce::Justification::centredLeft, tabGlobal);
    setupCaption (pitchTitle, "PITCH GLOBAL  /  SAMPLE ENTERO", juce::Justification::centredLeft, tabGlobal);
    setupCaption (pitchCaption, "DESDE EL ORIGINAL", juce::Justification::centred, tabGlobal);
    for (auto* l : { &globalTitle, &pitchTitle })
    {
        l->setFont (sans (13.0f));
        l->setColour (juce::Label::textColourId, kInk);
    }
    setupLcd (pitchLcd);
    pitchLcd.setFont (sans (19.0f));
    tabComponents[tabGlobal].push_back (&pitchLcd);

    setupKnob (master, "Master", " dB", tabGlobal);
    setupKnob (globalPan, "Pan", "", tabGlobal);
    setupKnob (tune, "Semitonos", " st", tabGlobal);
    master.slider.setColour (juce::Slider::thumbColourId, kRed);
    tune.slider.setColour (juce::Slider::thumbColourId, kRed);
    setupCaption (polyCaption, "POLI / MONO", juce::Justification::centredLeft, tabGlobal);
    setupCaption (voicesCaption, "VOCES", juce::Justification::centredLeft, tabGlobal);
    addToTab (tabGlobal, polyBox);
    addToTab (tabGlobal, voicesBox);
    polyBox.addItemList ({ "Poli", "Mono" }, 1);
    for (int v = 1; v <= 16; ++v)
        voicesBox.addItem (juce::String (v), v);

    //---- pestana AVANZADO
    setupCaption (advancedTitle, "AVANZADO", juce::Justification::centredLeft, tabAdvanced);
    advancedTitle.setFont (sans (13.0f));
    advancedTitle.setColour (juce::Label::textColourId, kInk);
    setupKnob (humanize, "Humanizar", " %", tabAdvanced);
    setupKnob (vel, "Vel", "", tabAdvanced);
    setupKnob (swing, "Swing", " %", tabAdvanced);
    setupCaption (chokeCaption, "CHOKE", juce::Justification::centredLeft, tabAdvanced);
    addToTab (tabAdvanced, chokeBox);
    chokeBox.addItemList ({ "Apagado", "Todos" }, 1);
    for (auto* b : { &pitchDownButton, &pitchUpButton, &pitchResetButton })
        addToTab (tabGlobal, *b);
    pitchDownButton.setColour (juce::TextButton::buttonColourId, kInkDim);
    pitchUpButton.setColour (juce::TextButton::buttonColourId, kInkDim);

    auto nudgePitch = [this] (float semitones)
    {
        if (auto* param = proc.apvts.getParameter ("tune"))
        {
            const float value = juce::jlimit (-12.0f, 12.0f, param->convertFrom0to1 (param->getValue()) + semitones);
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (value));
            param->endChangeGesture();
        }
    };
    pitchResetButton.onClick = [nudgePitch] { nudgePitch (-100.0f); nudgePitch (12.0f); };   // vuelve a 0
    pitchDownButton.onClick = [nudgePitch] { nudgePitch (-1.0f); };
    pitchUpButton.onClick = [nudgePitch] { nudgePitch (1.0f); };

    //---- pestana MIDI
    setupCaption (midiTitle, "MIDI", juce::Justification::centredLeft, tabMidi);
    midiTitle.setFont (sans (13.0f));
    midiTitle.setColour (juce::Label::textColourId, kInk);
    addToTab (tabMidi, learnButton);
    addToTab (tabMidi, keysButton);
    setupKnob (root, "Nota base", "", tabMidi);
    learnButton.setClickingTogglesState (true);
    learnButton.onClick = [this]
    {
        proc.engine.learnPad.store (learnButton.getToggleState() ? proc.getSelectedPad() : -1);
        padGrid.repaint();
    };

    //---- parametros
    tune.attachment = std::make_unique<SliderAttachment> (proc.apvts, "tune", tune.slider);
    master.attachment = std::make_unique<SliderAttachment> (proc.apvts, "master", master.slider);
    vel.attachment = std::make_unique<SliderAttachment> (proc.apvts, "vel", vel.slider);
    vel.slider.textFromValueFunction = [] (double v) { return juce::String ((int) std::lround (v * 100.0)) + " %"; };
    vel.slider.updateText();
    globalPan.attachment = std::make_unique<SliderAttachment> (proc.apvts, "pan", globalPan.slider);
    humanize.attachment = std::make_unique<SliderAttachment> (proc.apvts, "humanize", humanize.slider);
    swing.attachment = std::make_unique<SliderAttachment> (proc.apvts, "swing", swing.slider);
    polyAtt = std::make_unique<ComboAttachment> (proc.apvts, "mono", polyBox);
    voicesAtt = std::make_unique<ComboAttachment> (proc.apvts, "voices", voicesBox);
    chokeAtt = std::make_unique<ComboAttachment> (proc.apvts, "choke", chokeBox);
    invAtt = std::make_unique<ButtonAttachment> (proc.apvts, "inv", invButton);
    root.attachment = std::make_unique<SliderAttachment> (proc.apvts, "root", root.slider);
    // despues del attachment, que si no pisa el texto
    root.slider.textFromValueFunction = [] (double v) { return noteName ((int) std::lround (v)); };
    root.slider.updateText();
    syncAtt = std::make_unique<ButtonAttachment> (proc.apvts, "sync", syncButton);
    stretchAtt = std::make_unique<ButtonAttachment> (proc.apvts, "stretch", stretchButton);
    keysAtt = std::make_unique<ButtonAttachment> (proc.apvts, "keys", keysButton);

    sendLookAndFeelChange();   // los controles ya creados toman el estilo nuevo

    proc.addChangeListener (this);
    setPanelTab (tabPad);
    setViewMode (WaveformView::Mode::slices);
    refresh();
    startTimerHz (30);
    setSize (1000, 770);
}

SamplerEditor::~SamplerEditor()
{
    proc.removeChangeListener (this);
    proc.engine.learnPad.store (-1);
    setLookAndFeel (nullptr);
}

void SamplerEditor::addToTab (int tab, juce::Component& c)
{
    addChildComponent (c);
    tabComponents[tab].push_back (&c);
}

void SamplerEditor::setupCaption (juce::Label& l, const juce::String& text, juce::Justification just, int tab)
{
    if (tab >= 0)
        addToTab (tab, l);
    else
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
    l.setColour (juce::Label::textColourId, kRed);
    l.setJustificationType (juce::Justification::centred);
}

void SamplerEditor::setupKnob (Knob& k, const juce::String& name, const juce::String& suffix, int tab)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
    k.slider.setTextValueSuffix (suffix);
    addToTab (tab, k.slider);
    setupCaption (k.label, name.toUpperCase(), juce::Justification::centred, tab);
}

void SamplerEditor::setupPercentSlider (juce::Slider& s)
{
    addToTab (tabPad, s);
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 18);
    s.setRange (0.0, 100.0, 0.01);
    s.setNumDecimalPlacesToDisplay (1);
    s.setTextValueSuffix (" %");
}

void SamplerEditor::layoutKnob (Knob& k, juce::Rectangle<int> area)
{
    k.label.setBounds (area.removeFromTop (14));
    k.slider.setBounds (area.reduced (4, 0));
}

void SamplerEditor::setPanelTab (int tab)
{
    currentTab = juce::jlimit (0, (int) numTabs - 1, tab);
    for (int i = 0; i < numTabs; ++i)
    {
        panelTabs[i].setColour (juce::TextButton::buttonColourId, i == currentTab ? kRed : kKeyDark);
        for (auto* c : tabComponents[i])
            c->setVisible (i == currentTab);
    }
    repaint();
}

void SamplerEditor::setViewMode (WaveformView::Mode mode)
{
    waveform.setMode (mode);
    sourceTab.setColour (juce::TextButton::buttonColourId, mode == WaveformView::Mode::source ? kRed : kKeyDark);
    slicesTab.setColour (juce::TextButton::buttonColourId, mode == WaveformView::Mode::slices ? kRed : kKeyDark);
    analysisTab.setColour (juce::TextButton::buttonColourId, mode == WaveformView::Mode::analysis ? kRed : kKeyDark);
}

void SamplerEditor::showSliceMenu (int pad)
{
    const auto& slice = proc.getSlices()[(size_t) pad];
    if (! slice.used)
        return;

    juce::PopupMenu menu;
    menu.setLookAndFeel (&look);
    menu.addSectionHeader ("PAD " + juce::String (pad + 1) + "  /  " + proc.getSliceName (pad).toUpperCase());
    menu.addItem (1, "Renombrar");
    menu.addItem (2, "Favorito (no se pierde al regenerar)", true, slice.favorite);
    menu.addItem (3, "Fin automatico (hasta el corte siguiente)", slice.end > slice.start);
    menu.addSeparator();
    menu.addItem (4, "Borrar corte");

    juce::Component::SafePointer<SamplerEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withParentComponent (this),
                        [safe, pad] (int result)
                        {
                            if (safe == nullptr)
                                return;
                            auto& proc = safe->proc;
                            switch (result)
                            {
                                case 1:
                                    safe->setPanelTab (tabPad);
                                    safe->nameLabel.showEditor();
                                    break;
                                case 2: proc.setSliceFavorite (pad, ! proc.getSlices()[(size_t) pad].favorite); break;
                                case 3: proc.setSliceEnd (pad, -1); break;
                                case 4: proc.removeSlice (pad); break;
                                default: break;
                            }
                        });
}

void SamplerEditor::setPadGate (bool gate)
{
    if (auto* param = proc.apvts.getParameter (SamplerProcessor::padParamId (proc.getSelectedPad(), "gate")))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (gate ? 1.0f : 0.0f);
        param->endChangeGesture();
    }
    updateLive();
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
    padFine.attachment.reset();
    padVelAmt.attachment.reset();
    padPreGain.attachment.reset();
    padCutoff.attachment.reset();
    padReso.attachment.reset();
    reverseAtt.reset();
    loopAtt.reset();
    muteAtt.reset();
    filterAtt.reset();

    auto id = [pad] (const char* name) { return SamplerProcessor::padParamId (pad, name); };
    padGain.attachment = std::make_unique<SliderAttachment> (proc.apvts, id ("gain"), padGain.slider);
    padPan.attachment = std::make_unique<SliderAttachment> (proc.apvts, id ("pan"), padPan.slider);
    padPitch.attachment = std::make_unique<SliderAttachment> (proc.apvts, id ("pitch"), padPitch.slider);
    padAttack.attachment = std::make_unique<SliderAttachment> (proc.apvts, id ("attack"), padAttack.slider);
    padRelease.attachment = std::make_unique<SliderAttachment> (proc.apvts, id ("release"), padRelease.slider);
    padFine.attachment = std::make_unique<SliderAttachment> (proc.apvts, id ("fine"), padFine.slider);
    padVelAmt.attachment = std::make_unique<SliderAttachment> (proc.apvts, id ("velamt"), padVelAmt.slider);
    padPreGain.attachment = std::make_unique<SliderAttachment> (proc.apvts, id ("pregain"), padPreGain.slider);
    padCutoff.attachment = std::make_unique<SliderAttachment> (proc.apvts, id ("cutoff"), padCutoff.slider);
    padReso.attachment = std::make_unique<SliderAttachment> (proc.apvts, id ("reso"), padReso.slider);
    reverseAtt = std::make_unique<ButtonAttachment> (proc.apvts, id ("reverse"), reverseButton);
    loopAtt = std::make_unique<ButtonAttachment> (proc.apvts, id ("loop"), loopButton);
    muteAtt = std::make_unique<ButtonAttachment> (proc.apvts, id ("mute"), muteButton);
    filterAtt = std::make_unique<ComboAttachment> (proc.apvts, id ("filter"), filterBox);
}

void SamplerEditor::refresh()
{
    const int pad = proc.getSelectedPad();
    const auto& slice = proc.getSlices()[(size_t) pad];
    const auto src = proc.getSource();
    attachPad (pad);

    fileLabel.setText (proc.getFileName().isEmpty() ? juce::String ("SIN AUDIO") : proc.getFileName(),
                       juce::dontSendNotification);
    if (! bpmLabel.isBeingEdited())
        bpmLabel.setText (juce::String (proc.getSampleBpm(), 2), juce::dontSendNotification);
    infoLabel.setText (src != nullptr ? "BPM: " + juce::String (proc.getSampleBpm(), 2) + "    TONAL: --"
                                      : juce::String ("BPM: --    TONAL: --"),
                       juce::dontSendNotification);

    // pestana PAD
    padTitle.setText ("PAD " + juce::String (pad + 1).paddedLeft ('0', 2), juce::dontSendNotification);
    filterTitle.setText ("FILTRO DEL PAD " + juce::String (pad + 1).paddedLeft ('0', 2) + "  /  "
                             + proc.getSliceName (pad).toUpperCase(), juce::dontSendNotification);
    if (! nameLabel.isBeingEdited())
        nameLabel.setText (slice.used ? proc.getSliceName (pad) : juce::String ("VACIO"), juce::dontSendNotification);
    nameLabel.setEditable (false, slice.used);
    noteLabel.setText ("NOTA: " + noteName (proc.engine.padNote[(size_t) pad].load()), juce::dontSendNotification);
    favButton.setEnabled (slice.used);
    favButton.setColour (juce::TextButton::buttonColourId, slice.used && slice.favorite ? kRed : kInkDim);

    const double total = src != nullptr ? (double) src->buffer.getNumSamples() : 0.0;
    for (auto* s : { &startSlider, &endSlider })
        s->setEnabled (slice.used && total > 0.0);
    if (slice.used && total > 0.0)
    {
        startSlider.setValue ((double) slice.start / total * 100.0, juce::dontSendNotification);
        endSlider.setValue ((double) proc.getSliceEnd (pad) / total * 100.0, juce::dontSendNotification);
    }

    // cortes
    modeBox.setSelectedItemIndex ((int) proc.getSliceMode(), juce::dontSendNotification);
    gridBox.setSelectedItemIndex (proc.getGridIndex(), juce::dontSendNotification);
    gridBox.setEnabled (proc.getSliceMode() == eng::SliceMode::grid);
    sensSlider.setValue (proc.getSensitivity(), juce::dontSendNotification);
    sensSlider.setEnabled (proc.getSliceMode() == eng::SliceMode::transients);
    snapButton.setToggleState (proc.isSnapEnabled(), juce::dontSendNotification);
    snapBox.setSelectedItemIndex ((int) proc.getSnapMode(), juce::dontSendNotification);
    snapBox.setEnabled (proc.isSnapEnabled());

    // el tempo destino: el del proyecto si llega; si no, se escribe a mano
    const bool hostTempo = proc.hasHostTempo();
    tempoCaption.setText (hostTempo ? "PROYECTO" : "TEMPO", juce::dontSendNotification);
    if (tempoLabel.isEditable() == hostTempo)
        tempoLabel.setEditable (! hostTempo);
    if (! tempoLabel.isBeingEdited())
        tempoLabel.setText (juce::String (proc.getTargetBpm(), 1), juce::dontSendNotification);

    if (proc.engine.learnPad.load() >= 0 && proc.engine.learnPad.load() != pad)
        proc.engine.learnPad.store (pad);   // el aprendizaje sigue al pad elegido

    updateLive();
    repaint();
}

/** Lo que puede cambiar sin aviso (automatizacion del host, tempo, estado). */
void SamplerEditor::updateLive()
{
    statusLabel.setText (proc.getSource() != nullptr ? proc.getStatusText().toUpperCase() : juce::String(),
                         juce::dontSendNotification);

    const int semis = (int) std::lround (proc.apvts.getRawParameterValue ("tune")->load());
    pitchLcd.setText (semis == 0 ? juce::String ("0 ST") : (semis > 0 ? "+" : "") + juce::String (semis) + " ST",
                      juce::dontSendNotification);
    pitchLcd.setColour (juce::Label::textColourId, semis == 0 ? kLcdDim : kRed);

    const auto gateId = SamplerProcessor::padParamId (proc.getSelectedPad(), "gate");
    const bool gate = proc.apvts.getRawParameterValue (gateId)->load() > 0.5f;
    holdButton.setColour (juce::TextButton::buttonColourId, gate ? kRed : kKeyDark);
    shotButton.setColour (juce::TextButton::buttonColourId, gate ? kKeyDark : kRed);
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

    updateLive();
    waveform.repaint();
    overview.repaint();
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

    // marca: el numero va en rojo
    auto logo = logoBounds;
    const juce::String name (JucePlugin_Name);
    const auto word = name.trimCharactersAtEnd ("0123456789");
    const juce::Font logoFont (sans (30.0f));
    const int wordWidth = (int) std::ceil (juce::GlyphArrangement::getStringWidth (logoFont, word));
    g.setFont (logoFont);
    g.setColour (kInk);
    g.drawText (word, logo.removeFromLeft (wordWidth), juce::Justification::centredLeft);
    g.setColour (kRed);
    g.drawText (name.substring (word.length()), logo, juce::Justification::centredLeft);

    // pantalla
    g.setColour (kDisplay);
    g.fillRoundedRectangle (displayBounds.toFloat(), 9.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (displayBounds.toFloat().reduced (3.0f), 6.5f, 1.0f);

    // mini pantallas
    g.setColour (kDisplay);
    g.fillRoundedRectangle (bpmLabel.getBounds().toFloat(), 4.0f);
    g.fillRoundedRectangle (tempoLabel.getBounds().toFloat(), 4.0f);
    if (currentTab == tabGlobal)
        g.fillRoundedRectangle (pitchLcd.getBounds().toFloat(), 5.0f);

    // lineas de seccion
    g.setColour (kBodyLine);
    g.fillRect (18, padGrid.getY() - 7, getWidth() - 36, 1);
    g.fillRect (panelBounds.getX(), panelTabs[0].getBottom() + 4, panelBounds.getWidth(), 1);
    g.fillRect (18, footerBounds.getY() - 6, getWidth() - 36, 1);
    if (currentTab == tabGlobal)
        for (auto* title : { &globalTitle, &pitchTitle })
            g.fillRect (title->getX(), title->getBottom() + 1, title->getWidth(), 1);
    for (auto* title : { &filterTitle, &advancedTitle, &midiTitle })
        if (title->isVisible())
            g.fillRect (title->getX(), title->getBottom() + 1, title->getWidth(), 1);

    // pie: escudo, seccion MUESTRA y version
    auto footer = footerBounds;
    const auto crestArea = footer.removeFromLeft (70).toFloat();
    if (crest.isValid())
    {
        g.drawImage (crest, crestArea.reduced (1.0f), juce::RectanglePlacement::centred);
    }
    else
    {
        // lugar reservado para el escudo (Assets/escudo.png)
        juce::Path shield;
        const auto r = crestArea.reduced (12.0f, 10.0f);
        shield.startNewSubPath (r.getX(), r.getY());
        shield.lineTo (r.getRight(), r.getY());
        shield.lineTo (r.getRight(), r.getY() + r.getHeight() * 0.55f);
        shield.quadraticTo (r.getRight(), r.getBottom() - 4.0f, r.getCentreX(), r.getBottom());
        shield.quadraticTo (r.getX(), r.getBottom() - 4.0f, r.getX(), r.getY() + r.getHeight() * 0.55f);
        shield.closeSubPath();
        g.setColour (kBodyLine);
        g.strokePath (shield, juce::PathStrokeType (1.5f));
    }

    g.setColour (kInkDim);
    g.setFont (sans (10.0f));
    const auto credits = footerBounds.withTrimmedLeft (sampleBounds.getRight() - footerBounds.getX() + 20);
    g.drawText ("16 PAD SLICE SAMPLER", credits.withTrimmedBottom (credits.getHeight() / 2), juce::Justification::bottomRight);
    g.drawText (juce::String ("V") + JucePlugin_VersionString, credits.withTrimmedTop (credits.getHeight() / 2 + 4),
                juce::Justification::topRight);
}

void SamplerEditor::paintOverChildren (juce::Graphics& g)
{
    if (dragOver)
    {
        g.setColour (kRed);
        g.drawRoundedRectangle (displayBounds.toFloat().reduced (1.5f), 9.0f, 3.0f);
    }
}

void SamplerEditor::resized()
{
    auto area = getLocalBounds().reduced (18, 12);

    //---- pie: escudo + MUESTRA
    footerBounds = area.removeFromBottom (92);
    area.removeFromBottom (12);
    sampleBounds = footerBounds.withTrimmedLeft (84).withWidth (520);
    auto sample = sampleBounds;
    sampleCaption.setBounds (sample.removeFromTop (16));
    sample.removeFromTop (3);
    auto sampleButtons = sample.removeFromBottom (32);
    sample.removeFromBottom (4);
    overview.setBounds (sample);
    loadButton.setBounds (sampleButtons.removeFromLeft (100));
    sampleButtons.removeFromLeft (6);
    invButton.setBounds (sampleButtons.removeFromLeft (74));
    sampleButtons.removeFromLeft (6);
    silenceButton.setBounds (sampleButtons.removeFromLeft (100));
    zoomInButton.setBounds (sampleButtons.removeFromRight (40));
    sampleButtons.removeFromRight (4);
    zoomOutButton.setBounds (sampleButtons.removeFromRight (40));
    sampleButtons.removeFromRight (12);
    navRightButton.setBounds (sampleButtons.removeFromRight (40));
    sampleButtons.removeFromRight (4);
    navLeftButton.setBounds (sampleButtons.removeFromRight (40));

    //---- cabecera
    auto top = area.removeFromTop (36);
    logoBounds = top.removeFromLeft (190);
    stretchButton.setBounds (top.removeFromRight (100).withSizeKeepingCentre (100, 32));
    top.removeFromRight (6);
    syncButton.setBounds (top.removeFromRight (92).withSizeKeepingCentre (92, 32));
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
    top.removeFromRight (2);
    detectButton.setBounds (top.removeFromRight (86).withSizeKeepingCentre (86, 32));

    //---- vistas de la pantalla
    area.removeFromTop (8);
    auto views = area.removeFromTop (32);
    sourceTab.setBounds (views.removeFromLeft (84));
    views.removeFromLeft (4);
    slicesTab.setBounds (views.removeFromLeft (84));
    views.removeFromLeft (4);
    analysisTab.setBounds (views.removeFromLeft (92));
    resliceButton.setBounds (views.removeFromRight (120));
    views.removeFromRight (8);
    snapBox.setBounds (views.removeFromRight (150));
    views.removeFromRight (6);
    snapButton.setBounds (views.removeFromRight (124));
    infoLabel.setBounds (views.reduced (8, 0));

    //---- pantalla
    area.removeFromTop (6);
    displayBounds = area.removeFromTop (176);
    auto screen = displayBounds.reduced (12, 8);
    auto info = screen.removeFromTop (18);
    statusLabel.setBounds (info.removeFromRight (info.getWidth() / 2));
    fileLabel.setBounds (info);
    screen.removeFromTop (3);
    waveform.setBounds (screen);

    //---- fila de cortes
    area.removeFromTop (8);
    auto sliceRow = area.removeFromTop (32);
    modeCaption.setBounds (sliceRow.removeFromLeft (84));
    modeBox.setBounds (sliceRow.removeFromLeft (170));
    sliceRow.removeFromLeft (20);
    gridCaption.setBounds (sliceRow.removeFromLeft (54));
    gridBox.setBounds (sliceRow.removeFromLeft (160));
    sliceRow.removeFromLeft (20);
    sensCaption.setBounds (sliceRow.removeFromLeft (58));
    sensSlider.setBounds (sliceRow.removeFromLeft (170));

    //---- pads a la izquierda, panel con pestanas a la derecha
    area.removeFromTop (14);
    padGrid.setBounds (area.removeFromLeft (400).expanded (4, 4));
    area.removeFromLeft (26);
    panelBounds = area;

    auto tabs = area.removeFromTop (30);
    for (int i = 0; i < numTabs; ++i)
    {
        panelTabs[i].setBounds (tabs.removeFromLeft (104));
        tabs.removeFromLeft (4);
    }
    area.removeFromTop (12);
    const int kw = area.getWidth() / 5;

    // PAD
    {
        auto a = area;
        auto title = a.removeFromTop (26);
        padTitle.setBounds (title.removeFromLeft (70));
        favButton.setBounds (title.removeFromRight (96).withSizeKeepingCentre (96, 28));
        title.removeFromRight (10);
        noteLabel.setBounds (title.removeFromRight (90));
        nameLabel.setBounds (title);
        a.removeFromTop (8);

        auto knobs = a.removeFromTop (92);
        const int sw = knobs.getWidth() / 8;
        for (auto* k : { &padGain, &padPan, &padPitch, &padFine, &padVelAmt, &padPreGain, &padAttack, &padRelease })
            layoutKnob (*k, knobs.removeFromLeft (sw));
        a.removeFromTop (10);

        auto range = a.removeFromTop (28);
        auto left = range.removeFromLeft (range.getWidth() / 2 - 8);
        range.removeFromLeft (16);
        startCaption.setBounds (left.removeFromLeft (46));
        startSlider.setBounds (left);
        endCaption.setBounds (range.removeFromLeft (40));
        endSlider.setBounds (range);
        a.removeFromTop (12);

        auto row = a.removeFromTop (32);
        reverseButton.setBounds (row.removeFromLeft (104));
        row.removeFromLeft (6);
        loopButton.setBounds (row.removeFromLeft (80));
        row.removeFromLeft (6);
        muteButton.setBounds (row.removeFromLeft (112));
        a.removeFromTop (10);

        auto play = a.removeFromTop (32);
        playCaption.setBounds (play.removeFromLeft (104));
        holdButton.setBounds (play.removeFromLeft (110));
        play.removeFromLeft (4);
        shotButton.setBounds (play.removeFromLeft (100));
    }

    // FILTRO
    {
        auto a = area;
        filterTitle.setBounds (a.removeFromTop (20));
        a.removeFromTop (10);
        auto knobs = a.removeFromTop (102);
        auto column = knobs.removeFromLeft (170);
        filterCaption.setBounds (column.removeFromTop (14));
        column.removeFromTop (4);
        filterBox.setBounds (column.removeFromTop (32));
        knobs.removeFromLeft (16);
        layoutKnob (padCutoff, knobs.removeFromLeft (kw));
        layoutKnob (padReso, knobs.removeFromLeft (kw));
    }

    // GLOBAL
    {
        auto a = area;
        auto titles = a.removeFromTop (20);
        globalTitle.setBounds (titles.removeFromLeft (kw * 2 - 14));
        titles.removeFromLeft (14);
        pitchTitle.setBounds (titles);
        a.removeFromTop (10);

        auto knobs = a.removeFromTop (102);
        layoutKnob (master, knobs.removeFromLeft (kw));
        layoutKnob (globalPan, knobs.removeFromLeft (kw));
        layoutKnob (tune, knobs.removeFromLeft (kw));
        knobs.removeFromLeft (10);
        knobs = knobs.removeFromLeft (150);
        pitchCaption.setBounds (knobs.removeFromTop (14));
        pitchLcd.setBounds (knobs.withSizeKeepingCentre (knobs.getWidth(), 46).translated (0, -8));
        a.removeFromTop (10);

        auto captions = a.removeFromTop (14);
        polyCaption.setBounds (captions.removeFromLeft (kw));
        voicesCaption.setBounds (captions.removeFromLeft (kw));
        auto row = a.removeFromTop (32);
        polyBox.setBounds (row.removeFromLeft (kw - 8));
        row.removeFromLeft (8);
        voicesBox.setBounds (row.removeFromLeft (kw - 16));
        row.removeFromLeft (16);
        pitchDownButton.setBounds (row.removeFromLeft (52));
        row.removeFromLeft (6);
        pitchUpButton.setBounds (row.removeFromLeft (52));
        row.removeFromLeft (6);
        pitchResetButton.setBounds (row.removeFromLeft (110));
    }

    // AVANZADO
    {
        auto a = area;
        advancedTitle.setBounds (a.removeFromTop (20));
        a.removeFromTop (10);
        auto knobs = a.removeFromTop (102);
        layoutKnob (humanize, knobs.removeFromLeft (kw));
        layoutKnob (vel, knobs.removeFromLeft (kw));
        layoutKnob (swing, knobs.removeFromLeft (kw));
        a.removeFromTop (10);
        chokeCaption.setBounds (a.removeFromTop (14));
        chokeBox.setBounds (a.removeFromTop (32).removeFromLeft (170));
    }

    // MIDI
    {
        auto a = area;
        midiTitle.setBounds (a.removeFromTop (20));
        a.removeFromTop (10);
        auto knobs = a.removeFromTop (102);
        layoutKnob (root, knobs.removeFromLeft (kw));
        knobs.removeFromLeft (16);
        auto column = knobs.removeFromLeft (150);
        column.removeFromTop (14);
        keysButton.setBounds (column.removeFromTop (32));
        column.removeFromTop (8);
        learnButton.setBounds (column.removeFromTop (32));
    }
}

#include "Look.h"
#include "BinaryData.h"

namespace ui
{
/** La tipografia propia del plugin, embebida. */
juce::FontOptions sans (float height, bool)
{
    static const auto face = juce::Typeface::createSystemTypefaceFor (BinaryData::Sampler5Sans_ttf,
                                                                      (size_t) BinaryData::Sampler5Sans_ttfSize);
    return juce::FontOptions (face).withHeight (height * 1.12f).withKerningFactor (0.02f);
}

juce::FontOptions mono (float height)
{
    return sans (height);   // los numeros de la tipografia tienen todos el mismo ancho
}

juce::String noteName (int note)
{
    // FL Studio llama C5 a la nota 60
    return note < 0 ? "--" : juce::MidiMessage::getMidiNoteName (note, true, true, 5);
}

/** Dibuja una tecla con relieve y devuelve el rectangulo de su cara superior. */
juce::Rectangle<float> drawKey (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour,
                                bool down, float radius)
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

using namespace ui;

//==============================================================================
HardwareLook::HardwareLook()
{
    setColour (juce::ResizableWindow::backgroundColourId, kBody);
    setColour (juce::Label::textColourId, kInk);
    setColour (juce::Label::backgroundWhenEditingColourId, kDisplay);
    setColour (juce::Label::textWhenEditingColourId, kLcd);
    setColour (juce::Label::outlineWhenEditingColourId, kRed);
    setColour (juce::TextEditor::highlightColourId, kRed.withAlpha (0.6f));
    setColour (juce::TextEditor::highlightedTextColourId, juce::Colours::white);
    setColour (juce::CaretComponent::caretColourId, kRed);

    setColour (juce::TextButton::buttonColourId, kKeyDark);
    setColour (juce::TextButton::buttonOnColourId, kRed);
    setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ToggleButton::textColourId, kInk);

    setColour (juce::Slider::thumbColourId, kKeyDark);
    setColour (juce::Slider::textBoxTextColourId, kInk);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, kRed.withAlpha (0.6f));

    setColour (juce::ComboBox::textColourId, kInk);
    setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::PopupMenu::backgroundColourId, kKeyLight);
    setColour (juce::PopupMenu::textColourId, kInk);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kRed);
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
    drawLed (g, { face.getX() + 13.0f, face.getCentreY() }, 3.5f, kRed, b.getToggleState());
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
    g.setColour (kRed);
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
    g.setColour (kRed.withMultipliedAlpha (alpha));
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


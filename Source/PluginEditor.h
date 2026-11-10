#pragma once

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

// Interfaz provisoria: funcional y con la disposicion general definida,
// pero todavia sin el diseno final.

class WaveformView final : public juce::Component
{
public:
    explicit WaveformView (SamplerProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override { peaksFor = nullptr; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void rebuildPeaks();
    int markerAt (float x) const;
    float xForSample (juce::int64 sample) const;
    juce::int64 sampleForX (float x) const;

    SamplerProcessor& proc;
    const eng::AudioData* peaksFor = nullptr;
    std::vector<std::pair<float, float>> peaks;
    int dragging = -1;
};

class PadGrid final : public juce::Component
{
public:
    explicit PadGrid (SamplerProcessor&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> padBounds (int pad) const;
    int padAt (juce::Point<int>) const;

    SamplerProcessor& proc;
    int held = -1;
};

class SamplerEditor final : public juce::AudioProcessorEditor,
                            public juce::FileDragAndDropTarget,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    explicit SamplerEditor (SamplerProcessor&);
    ~SamplerEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override;
    void refresh();
    void attachPad (int pad);
    void setupKnob (Knob&, const juce::String& name, const juce::String& suffix);
    void layoutKnob (Knob&, juce::Rectangle<int>);

    SamplerProcessor& proc;
    WaveformView waveform;
    PadGrid padGrid;

    juce::TextButton loadButton { "Cargar" }, halfButton { "/2" }, doubleButton { "x2" },
                     sliceButton { "Cortar" }, learnButton { "Aprender MIDI" };
    juce::Label fileLabel, bpmCaption, bpmLabel, statusLabel, padTitle, modeCaption, gridCaption;
    juce::ComboBox modeBox, gridBox;
    juce::ToggleButton syncButton { "SYNC" }, stretchButton { "STRETCH" }, monoButton { "Mono" },
                       keysButton { "Teclas" }, reverseButton { "Reversa" }, gateButton { "Mantener" };

    Knob tune, master, vel, root;
    Knob padGain, padPan, padPitch, padAttack, padRelease;

    std::unique_ptr<ButtonAttachment> syncAtt, stretchAtt, monoAtt, keysAtt, reverseAtt, gateAtt;
    std::unique_ptr<juce::FileChooser> chooser;

    int attachedPad = -1;
    bool dragOver = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplerEditor)
};

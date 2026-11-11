#pragma once

#include "Look.h"
#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <functional>

/** La waveform de la pantalla, con tres vistas: FUENTE, CORTES y ANALISIS. */
class WaveformView final : public juce::Component
{
public:
    enum class Mode { source = 0, slices, analysis };

    explicit WaveformView (SamplerProcessor&);

    void setMode (Mode);
    Mode getMode() const { return mode; }

    /** Zoom: factor > 1 acerca. anchor es la posicion (0..1 del ancho) que queda quieta. */
    void zoom (double factor, double anchor = 0.5);
    void scroll (double fractionOfView);
    void centreOn (juce::int64 sample);
    juce::Range<double> getVisibleRange() const;

    std::function<void (int pad)> onSliceMenu;

    void paint (juce::Graphics&) override;
    void resized() override { peaksFor = nullptr; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    enum class Drag { none, sliceStart, sliceEnd, regionStart, regionEnd };

    void rebuildPeaks();
    void checkSource();
    double totalLength() const;
    int markerAt (float x) const;
    bool overSelectedEnd (float x) const;
    float xForSample (double sample) const;
    juce::int64 sampleForX (float x) const;
    juce::int64 snapTolerance() const;
    void paintSlices (juce::Graphics&, bool editable);
    void paintSource (juce::Graphics&);
    void paintAnalysis (juce::Graphics&);

    SamplerProcessor& proc;
    Mode mode = Mode::slices;
    double viewStart = 0.0, viewLength = 0.0;   // viewLength 0: todo el audio

    const eng::AudioData* peaksFor = nullptr;
    const eng::AudioData* viewFor = nullptr;
    double peaksStart = -1.0, peaksLength = -1.0;
    std::vector<std::pair<float, float>> peaks;

    Drag drag = Drag::none;
    int dragPad = -1;
};

/** Mini vista del audio entero: muestra que parte se ve en la pantalla y sirve para moverse. */
class OverviewStrip final : public juce::Component
{
public:
    OverviewStrip (SamplerProcessor&, WaveformView&);

    void paint (juce::Graphics&) override;
    void resized() override { peaksFor = nullptr; }
    void mouseDown (const juce::MouseEvent& e) override { mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent&) override;

private:
    SamplerProcessor& proc;
    WaveformView& waveform;
    const eng::AudioData* peaksFor = nullptr;
    std::vector<std::pair<float, float>> peaks;
};

class PadGrid final : public juce::Component
{
public:
    explicit PadGrid (SamplerProcessor&);

    std::function<void (int pad)> onSliceMenu;

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

    /** El host puede pedir su propia escala (pantallas de alta densidad): se combina con la elegida. */
    void setScaleFactor (float newScale) override;

    enum PanelTab { tabPad = 0, tabFilter, tabGlobal, tabAdvanced, tabMidi, numTabs };
    void setPanelTab (int tab);
    void setViewMode (WaveformView::Mode);

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override;
    void refresh();
    void updateLive();
    void attachPad (int pad);
    void showSliceMenu (int pad);
    void refreshPresets();
    void refreshKeyBox();
    void askPresetName();
    void applyScale();
    void applyTheme();
    static juce::File themeSettingsFile();
    static juce::File scaleSettingsFile();
    void setPadGate (bool gate);
    void setupKnob (Knob&, const juce::String& name, const juce::String& suffix, int tab);
    void layoutKnob (Knob&, juce::Rectangle<int>);
    void setupCaption (juce::Label&, const juce::String& text, juce::Justification, int tab = -1);
    void setupLcd (juce::Label&);
    void setupPercentSlider (juce::Slider&);
    void addToTab (int tab, juce::Component&);

    HardwareLook look;
    SamplerProcessor& proc;
    WaveformView waveform;
    OverviewStrip overview;
    PadGrid padGrid;

    // cabecera
    juce::TextButton detectButton { "Detectar" }, halfButton { "/2" }, doubleButton { "x2" };
    juce::Label bpmCaption, bpmLabel, tempoCaption, tempoLabel;
    juce::ToggleButton syncButton { "Sincro" }, stretchButton { "Estirar" };

    // fila de vistas y pantalla
    juce::TextButton sourceTab { "Fuente" }, slicesTab { "Cortes" }, analysisTab { "Analisis" },
                     resliceButton { "Regenerar" };
    juce::ToggleButton snapButton { "Autoajuste" };
    juce::ComboBox snapBox;
    juce::Label infoLabel, fileLabel, statusLabel;

    // fila de cortes
    juce::Label modeCaption, gridCaption, sensCaption;
    juce::ComboBox modeBox, gridBox;
    juce::Slider sensSlider;

    // panel derecho
    juce::TextButton panelTabs[numTabs];
    std::vector<juce::Component*> tabComponents[numTabs];

    juce::Label padTitle, nameLabel, noteLabel, startCaption, endCaption, playCaption;
    juce::TextButton favButton { "Favorito" }, holdButton { "Mantener" }, shotButton { "Disparo" };
    juce::ToggleButton reverseButton { "Invertir" }, loopButton { "Loop" }, muteButton { "Silenciar" };
    juce::Slider startSlider, endSlider;
    Knob padGain, padPan, padPitch, padFine, padVelAmt, padPreGain, padAttack, padRelease;

    juce::Label filterTitle, filterCaption;
    juce::ComboBox filterBox;
    Knob padCutoff, padReso;

    juce::Label globalTitle, pitchTitle, pitchCaption, pitchLcd;
    juce::Label polyCaption, voicesCaption, keyCaption;
    juce::ComboBox polyBox, voicesBox, keyBox;
    juce::TextButton pitchDownButton { "-1" }, pitchUpButton { "+1" }, pitchResetButton { "Original" };
    Knob master, globalPan, tune;

    juce::Label advancedTitle, chokeCaption, channelCaption;
    juce::ComboBox chokeBox, channelBox;
    Knob humanize, vel, swing;

    juce::Label midiTitle, rangeCaption, rangeDash, aftertouchCaption;
    juce::TextButton learnButton { "Aprender" };
    juce::ToggleButton keysButton { "Teclas" }, midiInButton { "Entrada MIDI" }, followButton { "Seguir pad" };
    juce::ComboBox rangeLoBox, rangeHiBox, aftertouchBox;

    // presets
    juce::Label presetCaption;
    juce::ComboBox presetBox;
    juce::TextButton presetSaveButton { "Guardar" }, presetFolderButton { "Carpeta" };
    juce::ComboBox sizeBox, themeBox;
    std::vector<juce::Label*> captionLabels, lcdLabels;
    juce::Rectangle<int> versionBounds;
    float hostScale = 1.0f, userScale = 1.0f;
    Knob root;

    // seccion MUESTRA
    juce::Label sampleCaption;
    juce::TextButton loadButton { "Cargar" }, navLeftButton { "<" }, navRightButton { ">" },
                     zoomOutButton { "-" }, zoomInButton { "+" }, silenceButton { "Silencio" };
    juce::ToggleButton invButton { "Inv" };

    std::unique_ptr<ButtonAttachment> syncAtt, stretchAtt, keysAtt, reverseAtt, loopAtt, muteAtt, invAtt;
    std::unique_ptr<ComboAttachment> polyAtt, voicesAtt, chokeAtt, filterAtt, channelAtt, rangeLoAtt, rangeHiAtt, aftertouchAtt;
    std::unique_ptr<ButtonAttachment> midiInAtt;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> displayBounds, logoBounds, footerBounds, panelBounds, sampleBounds;
    juce::Image crest;
    int attachedPad = -1, currentTab = tabPad;
    int shownKeyRoot = -2;
    bool shownKeyMinor = false;
    juce::String shownPreset;
    bool dragOver = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplerEditor)
};

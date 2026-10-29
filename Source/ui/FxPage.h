#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Style.h"
#include "PedalsPage.h"

class ValvedProcessor;

// Lista de tipos com LEDs (clique para escolher)
class TypeSelector : public juce::Component
{
public:
    TypeSelector (juce::RangedAudioParameter* p, juce::StringArray names, juce::Colour led);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void setSelected (int i) { if (i != selected) { selected = i; repaint(); } }
private:
    juce::RangedAudioParameter* param;
    juce::StringArray names;
    juce::Colour ledColour;
    int selected = 0;
    juce::Rectangle<float> itemRect (int i) const;
};

// Unidade de efeito (delay ou reverb) no estilo dos pedais de estúdio modernos
class FxUnit : public juce::Component
{
public:
    enum Kind { DelayUnit, ReverbUnit };
    FxUnit (ValvedProcessor& p, Kind kind);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    void renderBody();
    void tap();
    juce::String displayLine1() const;
    juce::String displayLine2() const;

    ValvedProcessor& proc;
    Kind kind;
    juce::Colour accentCol;
    std::vector<juce::String> ids;          // ids dos knobs
    juce::StringArray labels;
    juce::OwnedArray<juce::Slider> knobs;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> knobAtt;
    std::unique_ptr<TypeSelector> selector;
    FootSwitch onSwitch, tapSwitch { false };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt;
    ToggleSwitch syncSwitch { juce::Colour (0xffffb23a) };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAtt;
    juce::ComboBox division;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> divAtt;

    juce::Image body;
    juce::Rectangle<float> display, panel;
    juce::String shown1, shown2, specialLabel;
    bool onState = false, tapLed = false;
    std::vector<double> taps;
    double lastTapMs = 0.0;
};

class FxPage : public juce::Component
{
public:
    explicit FxPage (ValvedProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
private:
    FxUnit delayUnit, reverbUnit;
};

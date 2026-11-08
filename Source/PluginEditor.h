#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/Style.h"
#include "ui/AmpHead.h"
#include "ui/CabPage.h"
#include "ui/PedalsPage.h"
#include "ui/FxPage.h"

// Knob moderno da barra superior (nome em cima, valor embaixo)
class TopKnob : public juce::Component
{
public:
    TopKnob (juce::AudioProcessorValueTreeState& s, const char* paramId, const juce::String& name);
    void resized() override;
    juce::Slider slider;
private:
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class LevelMeter : public juce::Component
{
public:
    void setLevel (float l) { if (std::abs (l - level) > 0.001f) { level = l; repaint(); } }
    void paint (juce::Graphics&) override;
private:
    float level = 0;
};

class ValvedEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ValvedEditor (ValvedProcessor&);
    ~ValvedEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    enum Page { PagePedals = 0, PageAmp, PageCab, PageFx };
    void showPage (int page);
    void refreshNow() { timerCallback(); }

private:
    void timerCallback() override;
    static juce::String tuningName (int semis);

    ValvedProcessor& proc;
    ValvedLook look;

    juce::TextButton pedalsTab { "PEDALS" }, ampTab { "AMP" }, cabTab { "CAB" }, fxTab { "FX" };
    TopKnob input, gate, transpose, output;
    juce::Label tuningLabel;
    LevelMeter inMeter, outMeter;

    juce::OwnedArray<juce::TextButton> ampButtons;
    AmpHead head;
    CabPage cabPage;
    PedalsPage pedalsPage;
    FxPage fxPage;
    int page = PageAmp;

    juce::Rectangle<int> topBar, logoArea, mainArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ValvedEditor)
};

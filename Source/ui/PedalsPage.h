#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Style.h"

class ValvedProcessor;

// Chave de pé cromada (stomp switch)
class FootSwitch : public juce::Button
{
public:
    explicit FootSwitch (bool latching = true) : juce::Button ("footswitch") { setClickingTogglesState (latching); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

// Mini chave deslizante de dois lados (ex.: 808 / TS9)
class MiniToggle : public juce::ToggleButton
{
public:
    MiniToggle (juce::String left, juce::String right, juce::Colour inkColour)
        : leftText (std::move (left)), rightText (std::move (right)), ink (inkColour) {}
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
private:
    juce::String leftText, rightText;
    juce::Colour ink;
};

// Um pedal desenhado (overdrive ou compressor)
class Stompbox : public juce::Component
{
public:
    Stompbox (juce::AudioProcessorValueTreeState& s, int pedalIndex);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();

    juce::Point<float> jackIn() const;    // posição (no pai) das tomadas, para os cabos
    juce::Point<float> jackOut() const;

private:
    void renderBody();
    juce::Rectangle<float> knobRect (int k) const;

    juce::AudioProcessorValueTreeState& state;
    int index;               // 0..3 = drives, 4 = compressor
    int numKnobs = 3;
    juce::Slider knobs[4];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> knobAtt[4];
    std::unique_ptr<MiniToggle> modeSwitch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> modeAtt;
    FootSwitch footswitch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> switchAtt;
    juce::Image body;
    bool ledOn = false;
};

class PedalsPage : public juce::Component
{
public:
    explicit PedalsPage (ValvedProcessor& p);
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    juce::OwnedArray<Stompbox> pedals;
    juce::Rectangle<float> board;
};

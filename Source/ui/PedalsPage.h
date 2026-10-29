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

// Um pedal de overdrive desenhado
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
    int index;
    juce::Slider knobs[3];   // drive, tone, level
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> knobAtt[3];
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

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Style.h"

class ValvedProcessor;

// Alto-falante visto de frente: arraste o microfone sobre o cone
class SpeakerView : public juce::Component
{
public:
    explicit SpeakerView (juce::AudioProcessorValueTreeState& s);
    void paint (juce::Graphics&) override;
    void resized() override { background = {}; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void setActive (bool a) { if (a != active) { active = a; repaint(); } }

private:
    juce::Point<float> centre() const;
    float coneRadius() const;
    void setFromMouse (juce::Point<float>);
    void renderSpeaker();

    juce::RangedAudioParameter* px;
    juce::RangedAudioParameter* pd;
    float angle = -0.6f;      // só visual: o som depende da distância do centro
    bool active = true;
    juce::Image background;
};

class CabPage : public juce::Component
{
public:
    CabPage (ValvedProcessor& p);
    ~CabPage() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();   // chamado pelo timer do editor

private:
    void chooseIR();

    ValvedProcessor& proc;
    juce::OwnedArray<juce::TextButton> cabButtons;
    juce::TextButton loadIR;
    juce::Label irName;
    ToggleSwitch cabOn { juce::Colour (0xff3cff7a) };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> cabOnAtt;
    juce::Slider distance;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> distanceAtt;
    SpeakerView speaker;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> left, right;
    juce::String hint;
};

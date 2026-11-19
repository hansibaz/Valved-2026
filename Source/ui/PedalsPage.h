#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Style.h"
#include "ImgUI.h"

class ValvedProcessor;

// Chave de pé cromada (stomp switch) desenhada em código (ainda usada fora dos pedais em imagem)
class FootSwitch : public juce::Button
{
public:
    explicit FootSwitch (bool latching = true) : juce::Button ("footswitch") { setClickingTogglesState (latching); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

// Chave deslizante dos pedais em imagem (808 / TS9 no Green, CHO / VIB no Ensemble):
// trilho vazio da foto + a bolinha cromada, que vai para a esquerda ou para a direita
class SlideSwitch : public juce::ToggleButton
{
public:
    SlideSwitch() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    bool drawSlot = false;   // o trilho do Green já está na foto; o do Ensemble vem da imagem do trilho vazio
};

// Um pedal (foto do UI kit + controles por cima)
class Stompbox : public juce::Component
{
public:
    Stompbox (juce::AudioProcessorValueTreeState& s, int pedalIndex);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();

    float nativeWidth() const;    // tamanho útil da foto (px), para todos ficarem na mesma escala
    float nativeHeight() const;
    juce::Point<float> jackIn() const;    // posição (no pai) das tomadas, para os cabos
    juce::Point<float> jackOut() const;

private:
    imgui::Mapper mapper() const;

    juce::AudioProcessorValueTreeState& state;
    int index;               // 0..3 = drives (Green, Yellow, Gold, Blue), 4 = compressor (guardado, fora da tela), 5 = chorus
    int numKnobs = 3;
    juce::Slider knobs[4];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> knobAtt[4];
    std::unique_ptr<SlideSwitch> modeSwitch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> modeAtt;
    imgui::HitButton footswitch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> switchAtt;
    juce::Image body, ledOff;
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

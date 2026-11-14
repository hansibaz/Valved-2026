#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Style.h"

class ValvedProcessor;

// Chave moderna (deslizante, com LED embutido). Vertical: para cima = ligada.
class ModernSwitch : public juce::Button
{
public:
    explicit ModernSwitch (bool vertical, juce::Colour ledColour = juce::Colour (0xff3cff7a))
        : juce::Button ({}), isVertical (vertical), led (ledColour) {}
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
private:
    bool isVertical;
    juce::Colour led;
    float pos = -1.0f;    // posição animada da alavanca (0 = desligada, 1 = ligada)
    struct Anim : juce::Timer { ModernSwitch* s = nullptr; void timerCallback() override; } anim;
    friend struct Anim;
};

// Frente de uma caixa 4x12: um falante inteiro (onde fica o microfone), parte do falante da
// direita e parte do de baixo, com tela na frente e logo. Arraste o microfone sobre o cone.
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
    float speakerRadius() const;
    float coneRadius() const;
    void setFromMouse (juce::Point<float>);
    void renderCab();

    juce::AudioProcessorValueTreeState& state;
    juce::RangedAudioParameter* px;
    juce::RangedAudioParameter* pd;
    float angle = -2.35f;     // só visual: o som depende da distância do centro
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
    void setCab (int index);

    ValvedProcessor& proc;
    juce::OwnedArray<juce::TextButton> cabButtons;
    juce::TextButton loadIR;
    juce::Label irName;
    ModernSwitch irSwitch { true };                                  // IR: para cima só com IR carregado
    ModernSwitch cabOn { false, juce::Colour (0xfff2a23a) };        // liga/desliga a simulação de caixa
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> cabOnAtt;
    int lastBuiltInCab = 0;
    juce::Slider distance;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> distanceAtt;
    // Microfone: modelo, ângulo e as chaves do SM7B
    juce::OwnedArray<juce::TextButton> micButtons;
    juce::TextButton bassCut { "BASS CUT" }, presence { "PRESENCE" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bassCutAtt, presenceAtt;
    juce::OwnedArray<juce::TextButton> mdBass;   // MD421: chave de graves M, 2, 3, 4, S
    juce::Slider angle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> angleAtt;
    SpeakerView speaker;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> left, micPanel;
    juce::Rectangle<int> switchesRow;
};

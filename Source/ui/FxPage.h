#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Style.h"
#include "ImgUI.h"
#include "PedalsPage.h"

class ValvedProcessor;

// Botão sem desenho (a foto já mostra o botão)
struct ClearButton : public juce::Button
{
    ClearButton() : juce::Button ("clear") { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paintButton (juce::Graphics&, bool, bool) override {}
};

// Chave de alavanca do SYNC (foto da chave; desligada = alavanca para o outro lado)
struct LeverSwitch : public juce::ToggleButton
{
    LeverSwitch() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paintButton (juce::Graphics&, bool, bool) override;
};

// Unidade de efeito (delay ou reverb) com a foto do UI kit "POST FX"
class FxUnit : public juce::Component
{
public:
    enum Kind { DelayUnit, ReverbUnit };
    FxUnit (ValvedProcessor& p, Kind kind);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void refresh();

    float nativeWidth() const;
    float nativeHeight() const;

private:
    imgui::Mapper mapper() const;
    void tap();
    juce::String displayLine1() const;
    juce::String displayLine2() const;
    juce::Rectangle<float> itemRect (int typeIndex) const;   // na foto original

    ValvedProcessor& proc;
    Kind kind;
    juce::Colour accentCol;
    std::vector<juce::String> ids;          // ids dos knobs
    juce::OwnedArray<juce::Slider> knobs;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> knobAtt;
    imgui::HitButton onSwitch, tapSwitch { false };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt;
    LeverSwitch syncSwitch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAtt;
    juce::ComboBox division;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> divAtt;

    juce::Image body, listLed, ledOn, ledOff, syncOn, syncOff, tapOn;
    juce::String shown1, shown2, specialLabel, divText;
    int selected = -1;
    bool onState = false, tapLed = false, syncState = false;
    std::vector<double> taps;
    double lastTapMs = 0.0;
};

// Fader do EQ gráfico: a capa (foto) corre no trilho da foto
class EqFader : public juce::Slider
{
public:
    EqFader()
    {
        setSliderStyle (juce::Slider::LinearVertical);
        setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        setPopupDisplayEnabled (true, true, nullptr);
        setMouseDragSensitivity (200);
    }
    void paint (juce::Graphics&) override;
    float capW = 20.0f, capH = 11.0f;
};

// Pedal de EQ gráfico (post FX): VALVED 7 bandas ou MARK 5 bandas
class EqUnit : public juce::Component
{
public:
    explicit EqUnit (ValvedProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
    float nativeWidth() const { return 793.0f; }
    float nativeHeight() const { return 1659.0f; }
private:
    imgui::Mapper mapper() const;
    ValvedProcessor& proc;
    juce::OwnedArray<EqFader> vFaders, mFaders;
    EqFader level;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> att;
    ClearButton modeV, modeM;
    imgui::HitButton onSwitch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt;
    juce::Image body, btnOn, btnOff, ledOff;
    int mode = -1;
    bool onState = false;
};

class FxPage : public juce::Component
{
public:
    explicit FxPage (ValvedProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
private:
    EqUnit eqUnit;
    FxUnit delayUnit, reverbUnit;
};

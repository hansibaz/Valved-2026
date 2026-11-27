#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/Style.h"
#include "ui/AmpHead.h"
#include "ui/CabPage.h"
#include "ui/PedalsPage.h"
#include "ui/FxPage.h"
#include "ui/TunerView.h"
#include "ui/PresetBar.h"
#include "ui/ActivationView.h"
#include "ui/BackingBar.h"

// Knob moderno da barra superior (nome em cima, valor embaixo)
class TopKnob : public juce::Component
{
public:
    TopKnob (juce::AudioProcessorValueTreeState& s, const char* paramId, const juce::String& name);
    void resized() override;
    juce::Slider slider;
    juce::Label label;
private:
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
    void refreshLanguage();

    // bandeirinha do seletor de idioma
    struct FlagButton : public juce::Button
    {
        explicit FlagButton (int l) : juce::Button ("flag"), language (l) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        void paintButton (juce::Graphics&, bool, bool) override;
        int language;
    };
    void layoutHead();

private:
    void timerCallback() override;
    static juce::String tuningName (int semis);

    ValvedProcessor& proc;
    ValvedLook look;

    juce::TextButton pedalsTab { "PEDALS" }, ampTab { "AMP" }, cabTab { "CAB" }, fxTab { "FX" };
    TopKnob input, gate, transpose, output;
    juce::Label tuningLabel;
    FlagButton flagEn { lang::English }, flagPt { lang::Portuguese };
    PresetBar presetBar;
    juce::TextButton tunerBtn { "TUNER" };
    // engrenagem: configurações de áudio (interface, entrada/saída, buffer) — só na versão standalone
    struct GearButton : public juce::Button
    {
        GearButton() : juce::Button ("audio") { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        void paintButton (juce::Graphics&, bool over, bool down) override;
    };
    GearButton audioBtn;
    juce::TextButton camBtn { "CAM" };   // Valved Cam (app Android): grava o vídeo do celular com o som do Valved
    int lastCamStatus = -1;
    juce::TextButton qualityBtn[3] { juce::TextButton ("LOW"), juce::TextButton ("MED"), juce::TextButton ("HIGH") };
    juce::Rectangle<int> qualityLabelArea;
    LevelMeter inMeter, outMeter;

    juce::OwnedArray<juce::TextButton> ampButtons;
    // Motor do amp: CIRCUIT (primeira versão) e NEURAL (capturas dos amps reais)
    juce::OwnedArray<juce::TextButton> engineButtons;
    AmpHead head;
    CabPage cabPage;
    PedalsPage pedalsPage;
    FxPage fxPage;
    TunerView tunerView;
    BackingBar backingBar;   // base para tocar junto (embaixo)
    int page = PageAmp;

    juce::Rectangle<int> topBar, logoArea, mainArea;
    static constexpr int backingH = 100;
    std::unique_ptr<ActivationView> activation;   // tela de ativação (só aparece sem licença)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ValvedEditor)
};

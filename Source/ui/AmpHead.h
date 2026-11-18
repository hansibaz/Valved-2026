#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Style.h"

// Desenha o "head" completo de cada amplificador, com os controles no painel.
// Recriação em código dos designs de referência do Valved (CLEAN 120, BRIT80, 51 BLOCK, REC).
class AmpHead : public juce::Component
{
public:
    explicit AmpHead (juce::AudioProcessorValueTreeState& state);
    ~AmpHead() override;

    void setModel (int model);
    int getModel() const { return model; }
    bool isImageAmp() const { return imageMode(); }
    void refreshState();   // chamado pelo timer

    void paint (juce::Graphics&) override;
    void resized() override;

    // Controle no painel: knob ou chave
    struct Control
    {
        const char* paramId;
        const char* label;
        bool isSwitch;
        int group;          // seção do painel (cada seção tem um título)
    };

    struct Geometry
    {
        juce::Rectangle<float> body, front, panel, grille, handle;
        std::vector<juce::Point<float>> pos;   // centro de cada controle
        float knobD = 40.0f;
        float labelY = 0.0f, titleY = 0.0f;
    };

private:
    const std::vector<Control>& controls() const;
    Geometry computeGeometry() const;
    void renderBackground();
    void layoutControls();

    void drawCabinet (juce::Graphics&, const Geometry&);
    void drawClean (juce::Graphics&, const Geometry&);
    void drawBrit (juce::Graphics&, const Geometry&);
    void drawBlock (juce::Graphics&, const Geometry&);
    void drawRec (juce::Graphics&, const Geometry&);
    void drawControlsArt (juce::Graphics&, const Geometry&, juce::Colour ink, juce::Colour titleInk, juce::Colour lineInk);

    juce::AudioProcessorValueTreeState& state;
    int model = 1;

    // Um slider por parâmetro (aparece só no amp que tem esse controle)
    juce::OwnedArray<juce::Slider> sliders;
    juce::StringArray sliderIds;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAtt;

    juce::OwnedArray<ToggleSwitch> switches;
    juce::StringArray switchIds;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> switchAtt;

    juce::Slider* findSlider (const juce::String& id);
    static juce::String fullId (int model, const char* base);
    ToggleSwitch* findSwitch (const juce::String& id);

    // Amps com a interface em imagem (UI kits do 51 BLOCK e do REC): foto + knobs/chaves interativos
    bool imageMode() const { return model == 2 || model == 3 || model == 4; }   // 51 Block, REC e REC Version 2
public:
    struct ImageSpec
    {
        float w, h, anchorY;                 // tamanho da foto e onde cortar (0 = só embaixo, 1 = só em cima)
        const float* knobX; const float* knobY; int numKnobs; float knobSize;
        juce::Point<float> power, standby, led;   // centros
        juce::Point<float> toggleSize; float ledSize;
    };
private:
    const ImageSpec& spec() const;
    juce::Rectangle<float> imageRect() const;
    juce::Point<float> fromImage (float x, float y) const;
    juce::Rectangle<int> imageBox (float cx, float cy, float w, float h) const;
    struct RecToggle : public juce::Button
    {
        RecToggle();
        void paintButton (juce::Graphics&, bool, bool) override;
        juce::Image offImg;
    };
    void refreshImageAssets();
    std::unique_ptr<RecToggle> recPower, recStandby;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> recPowerAtt, recStandbyAtt;
    juce::Image recBg, recBgV2, recLedOff;
    juce::Image blockBg, blockLedOff, recPowerOff, recStandbyOff, blockPowerOff, blockStandbyOff;
    bool powerShown = true;

    juce::Image cache;
    int cacheModel = -1;
    juce::Rectangle<int> cacheBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpHead)
};

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Style.h"

// Desenha o "head" completo de cada amplificador, com os controles no painel.
// Recriação em código dos designs de referência do Valved (CLEAN 120, BRIT410, 51 BLOCK, REC).
class AmpHead : public juce::Component
{
public:
    explicit AmpHead (juce::AudioProcessorValueTreeState& state);
    ~AmpHead() override;

    void setModel (int model);
    int getModel() const { return model; }
    void refreshState();   // chamado pelo timer (ex.: escurecer Rate/Depth com chorus desligado)

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
    ToggleSwitch* findSwitch (const juce::String& id);

    juce::Image cache;
    int cacheModel = -1;
    juce::Rectangle<int> cacheBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpHead)
};

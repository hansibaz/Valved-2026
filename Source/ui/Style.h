#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{
    // Paleta geral (estilo escuro, como os plugins modernos)
    const juce::Colour bg       { 0xff0b0b0d };
    const juce::Colour bar      { 0xff121215 };
    const juce::Colour panel    { 0xff17171b };
    const juce::Colour panelHi  { 0xff222228 };
    const juce::Colour line     { 0xff2c2c33 };
    const juce::Colour text     { 0xffeeeeee };
    const juce::Colour textDim  { 0xff8a8a94 };
    const juce::Colour accent   { 0xfff2a23a };   // laranja/âmbar do raio
    const juce::Colour accent2  { 0xffff6a1f };

    // Fontes embutidas (licença OFL)
    struct Fonts
    {
        static juce::Font condensed (float h);   // Barlow Condensed SemiBold
        static juce::Font condensedBold (float h);
        static juce::Font condensedMedium (float h);
        static juce::Font logo (float h);        // Barlow Condensed Black Italic
        static juce::Font badge (float h);       // Russo One
        static juce::Font signature (float h);   // Mrs Saint Delafield (letra de mão)
        static juce::Font wide (float h);        // Michroma (moderna, larga)
        static juce::Font serif (float h);       // Cinzel (serifada)
    };

    // Logo "VALVED" com raio sublinhando a palavra
    void drawLogo (juce::Graphics& g, juce::Rectangle<float> area);

    // Estilos de knob (guardados em slider.getProperties()["knobStyle"])
    enum KnobStyle { KnobModern = 0, KnobSilver, KnobGoldCap, KnobChrome, KnobBlackChrome, KnobBlackPointer, KnobBrass, KnobCream };
}

class ValvedLook : public juce::LookAndFeel_V4
{
public:
    ValvedLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    static void drawModernKnob (juce::Graphics&, juce::Rectangle<float>, float pos, float a0, float a1);
    static void drawAmpKnob (juce::Graphics&, juce::Rectangle<float>, float angle, int style);
};

// Chave de alavanca (toggle) desenhada, com LED
class ToggleSwitch : public juce::ToggleButton
{
public:
    explicit ToggleSwitch (juce::Colour ledColour = juce::Colour (0xffff3b2f)) : led (ledColour) {}
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
private:
    juce::Colour led;
};

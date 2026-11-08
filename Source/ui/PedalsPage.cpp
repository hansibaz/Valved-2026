#include "PedalsPage.h"
#include "../PluginProcessor.h"

using namespace ui;

namespace
{
    struct KnobSpot { float x, y, size; };
    struct PedalLook
    {
        juce::Colour top, bottom, ink;
        int knobStyle;
        int numKnobs;
        KnobSpot spots[4];
        const char* labels[4];
        const char* subtitle;
    };

    // 0..3 = drives (Green, Yellow, Gold, Blue), 4 = compressor (Silver Comp)
    const PedalLook looks[5] = {
        { juce::Colour (0xff4aa35c), juce::Colour (0xff1d5c2c), juce::Colour (0xfff3ecd6), KnobBlackPointer, 3,
          { { 0.27f, 0.18f, 0.28f }, { 0.50f, 0.30f, 0.18f }, { 0.73f, 0.18f, 0.28f } },
          { "DRIVE", "TONE", "LEVEL" }, "OVERDRIVE" },
        { juce::Colour (0xfff5cd2a), juce::Colour (0xffc99706), juce::Colour (0xff151208), KnobBlackPointer, 3,
          { { 0.78f, 0.21f, 0.23f }, { 0.50f, 0.21f, 0.23f }, { 0.22f, 0.21f, 0.23f } },
          { "DRIVE", "TONE", "LEVEL" }, "ASYMMETRIC OVERDRIVE" },
        { juce::Colour (0xffe6c473), juce::Colour (0xff97762c), juce::Colour (0xff2b1f0c), KnobCream, 3,
          { { 0.22f, 0.21f, 0.23f }, { 0.50f, 0.21f, 0.23f }, { 0.78f, 0.21f, 0.23f } },
          { "GAIN", "TREBLE", "OUTPUT" }, "TRANSPARENT OVERDRIVE" },
        { juce::Colour (0xff3a6fd8), juce::Colour (0xff15367f), juce::Colours::white, KnobChrome, 3,
          { { 0.50f, 0.21f, 0.23f }, { 0.78f, 0.21f, 0.23f }, { 0.22f, 0.21f, 0.23f } },
          { "DRIVE", "TONE", "VOLUME" }, "AMP-IN-A-BOX" },
        { juce::Colour (0xffe9ebee), juce::Colour (0xff8d9198), juce::Colour (0xff15171a), KnobBlackPointer, 4,
          { { 0.27f, 0.17f, 0.26f }, { 0.73f, 0.17f, 0.26f }, { 0.27f, 0.355f, 0.16f }, { 0.73f, 0.355f, 0.16f } },
          { "SUSTAIN", "LEVEL", "ATTACK", "CLIP" }, "OTA COMPRESSOR" },
    };

    bool isComp (int look) { return look == 4; }

    juce::String knobParam (int look, int k)
    {
        if (isComp (look))
        {
            static const char* ids[4] = { ParamID::compSustain, ParamID::compLevel, ParamID::compAttack, ParamID::compClip };
            return ids[k];
        }
        static const char* suffix[3] = { "Drive", "Tone", "Level" };
        return ParamID::pedal (look, suffix[k]);
    }

    juce::String onParam (int look) { return isComp (look) ? juce::String (ParamID::compOn) : ParamID::pedal (look, "On"); }

    constexpr float knobStart = juce::MathConstants<float>::pi * (7.0f / 6.0f);
    constexpr float knobEnd   = juce::MathConstants<float>::pi * (17.0f / 6.0f);

    juce::Path rounded (juce::Rectangle<float> r, float radius) { juce::Path p; p.addRoundedRectangle (r, radius); return p; }

    juce::ColourGradient chrome (juce::Rectangle<float> r)
    {
        juce::ColourGradient cg (juce::Colour (0xfffafafa), r.getX(), r.getY(), juce::Colour (0xff595c62), r.getX(), r.getBottom(), false);
        cg.addColour (0.42, juce::Colour (0xffc5c8cd));
        cg.addColour (0.55, juce::Colour (0xff7e8289));
        cg.addColour (0.8, juce::Colour (0xffb7babf));
        return cg;
    }

    void jackSocket (juce::Graphics& g, juce::Point<float> c, float r)
    {
        g.setGradientFill (chrome ({ c.x - r, c.y - r, r * 2, r * 2 }));
        g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
        g.setColour (juce::Colour (0xff050505));
        g.fillEllipse (c.x - r * 0.55f, c.y - r * 0.55f, r * 1.1f, r * 1.1f);
    }
}

//==============================================================================
void FootSwitch::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat();
    const float R = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f - 2.0f;
    const auto c = r.getCentre();

    // Porca sextavada
    juce::Path hex;
    for (int k = 0; k < 6; ++k)
    {
        const float a = juce::MathConstants<float>::pi / 3.0f * (float) k + juce::MathConstants<float>::pi / 6.0f;
        if (k == 0) hex.startNewSubPath (c.x + std::cos (a) * R, c.y + std::sin (a) * R);
        else hex.lineTo (c.x + std::cos (a) * R, c.y + std::sin (a) * R);
    }
    hex.closeSubPath();
    juce::DropShadow (juce::Colours::black.withAlpha (0.7f), 8, { 0, 4 }).drawForPath (g, hex);
    g.setGradientFill (chrome (r));
    g.fillPath (hex);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.strokePath (hex, juce::PathStrokeType (1.0f));

    // Botão abaulado
    const float br = R * (down ? 0.6f : 0.64f);
    juce::ColourGradient dome (juce::Colour (0xffffffff), c.x - br * 0.4f, c.y - br * 0.6f, juce::Colour (0xff4b4e54), c.x + br * 0.5f, c.y + br, true);
    dome.addColour (0.5, juce::Colour (0xffaeb1b6));
    g.setGradientFill (dome);
    g.fillEllipse (c.x - br, c.y - br, br * 2, br * 2);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawEllipse (c.x - br, c.y - br, br * 2, br * 2, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.65f));
    g.fillEllipse (c.x - br * 0.5f, c.y - br * 0.62f, br * 0.55f, br * 0.3f);
    if (highlighted)
    {
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillEllipse (c.x - br, c.y - br, br * 2, br * 2);
    }
}

//==============================================================================
void MiniToggle::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();
    auto slot = juce::Rectangle<float> (r.getWidth() * 0.34f, r.getHeight() * 0.62f).withCentre (r.getCentre());
    // Rótulos dos dois lados
    g.setFont (Fonts::condensedBold (juce::jmax (12.0f, r.getHeight() * 0.55f)));
    g.setColour (ink.withAlpha (on ? 0.5f : 1.0f));
    g.drawText (leftText,  r.withRight (slot.getX() - 3.0f), juce::Justification::centredRight);
    g.setColour (ink.withAlpha (on ? 1.0f : 0.5f));
    g.drawText (rightText, r.withLeft (slot.getRight() + 3.0f), juce::Justification::centredLeft);

    // Trilho da chave deslizante
    g.setColour (juce::Colour (0xff0b0b0c));
    g.fillRoundedRectangle (slot, slot.getHeight() * 0.5f);
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawRoundedRectangle (slot.translated (0, 0.8f), slot.getHeight() * 0.5f, 0.8f);
    const float d = slot.getHeight() - 3.0f;
    auto knob = juce::Rectangle<float> (d, d).withCentre ({ on ? slot.getRight() - d * 0.5f - 1.5f : slot.getX() + d * 0.5f + 1.5f, slot.getCentreY() });
    juce::ColourGradient kg (juce::Colour (0xfff6f6f6), knob.getX(), knob.getY(), juce::Colour (0xff60636a), knob.getX(), knob.getBottom(), false);
    g.setGradientFill (kg);
    g.fillEllipse (knob);
    if (highlighted) { g.setColour (juce::Colours::white.withAlpha (0.15f)); g.fillEllipse (knob); }
}

//==============================================================================
Stompbox::Stompbox (juce::AudioProcessorValueTreeState& s, int pedalIndex) : state (s), index (pedalIndex)
{
    numKnobs = looks[index].numKnobs;
    for (int k = 0; k < numKnobs; ++k)
    {
        auto& kn = knobs[k];
        kn.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        kn.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        kn.setRotaryParameters (knobStart, knobEnd, true);
        kn.setPopupDisplayEnabled (true, true, nullptr);
        kn.setMouseDragSensitivity (200);
        kn.getProperties().set ("knobStyle", looks[index].knobStyle);
        addAndMakeVisible (kn);
        const auto id = knobParam (index, k);
        knobAtt[k] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, kn);
        if (auto* prm = s.getParameter (id))
            kn.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    }
    addAndMakeVisible (footswitch);
    switchAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, onParam (index), footswitch);

    if (index == 0)   // Green: chave TS808 / TS9
    {
        modeSwitch = std::make_unique<MiniToggle> ("808", "TS9", looks[0].ink);
        modeSwitch->setTooltip ("TS808 or TS9 circuit (output stage / op-amp)");
        addAndMakeVisible (*modeSwitch);
        modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, ParamID::greenMode, *modeSwitch);
    }
}

juce::Rectangle<float> Stompbox::knobRect (int k) const
{
    const auto& sp = looks[index].spots[k];
    const float W = (float) getWidth(), H = (float) getHeight();
    const float d = W * sp.size;
    return juce::Rectangle<float> (d, d).withCentre ({ W * sp.x, H * sp.y });
}

void Stompbox::resized()
{
    for (int k = 0; k < numKnobs; ++k) knobs[k].setBounds (knobRect (k).toNearestInt());
    const float W = (float) getWidth(), H = (float) getHeight();
    if (modeSwitch != nullptr)
        modeSwitch->setBounds (juce::Rectangle<float> (W * 0.8f, 24.0f).withCentre ({ W * 0.5f, H * 0.685f }).toNearestInt());
    const float fsz = W * 0.32f;
    footswitch.setBounds (juce::Rectangle<float> (fsz, fsz).withCentre ({ W * 0.5f, H * 0.82f }).toNearestInt());
    body = {};
}

juce::Point<float> Stompbox::jackIn() const  { return getBounds().toFloat().getTopLeft().translated ((float) getWidth() * 0.22f, 2.0f); }
juce::Point<float> Stompbox::jackOut() const { return getBounds().toFloat().getTopLeft().translated ((float) getWidth() * 0.78f, 2.0f); }

void Stompbox::renderBody()
{
    const float scale = 2.0f;
    body = juce::Image (juce::Image::ARGB, juce::jmax (1, (int) (getWidth() * scale)), juce::jmax (1, (int) (getHeight() * scale)), true);
    juce::Graphics g (body);
    g.addTransform (juce::AffineTransform::scale (scale));

    const auto& lk = looks[index];
    const auto r = getLocalBounds().toFloat().reduced (4.0f, 6.0f);
    const float W = r.getWidth(), H = r.getHeight();
    const float R = W * 0.07f;
    const auto shape = rounded (r, R);

    juce::DropShadow (juce::Colours::black.withAlpha (0.85f), 22, { 0, 12 }).drawForPath (g, shape);

    // Caixa de alumínio pintada
    juce::ColourGradient bodyGrad (lk.top, r.getX(), r.getY(), lk.bottom, r.getX() + W * 0.3f, r.getBottom(), false);
    g.setGradientFill (bodyGrad);
    g.fillPath (shape);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (shape);
        juce::Random rng (100 + index);
        if (index == 2 || isComp (index))
        {
            // metal escovado (dourado / alumínio)
            for (float y = r.getY(); y < r.getBottom(); y += 0.75f)
            {
                g.setColour ((rng.nextBool() ? juce::Colours::white : (isComp (index) ? juce::Colour (0xff202328) : juce::Colour (0xff3b2a0e))).withAlpha (0.12f * rng.nextFloat()));
                g.fillRect (r.getX(), y, W, 0.5f);
            }
        }
        else
        {
            // pintura metálica (flocos)
            for (int i = 0; i < (int) (W * H * 0.35f); ++i)
            {
                g.setColour ((rng.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.06f * rng.nextFloat()));
                g.fillRect (r.getX() + rng.nextFloat() * W, r.getY() + rng.nextFloat() * H, 1.0f, 1.0f);
            }
        }
        // brilho de estúdio na diagonal e bordas arredondadas
        juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.22f), r.getX(), r.getY(), juce::Colours::transparentWhite, r.getX() + W * 0.6f, r.getY() + H * 0.4f, false);
        g.setGradientFill (sheen);
        g.fillRect (r);
        juce::ColourGradient edge (juce::Colours::black.withAlpha (0.35f), r.getX(), 0, juce::Colours::black.withAlpha (0.35f), r.getRight(), 0, false);
        edge.addColour (0.07, juce::Colours::transparentBlack);
        edge.addColour (0.93, juce::Colours::transparentBlack);
        g.setGradientFill (edge);
        g.fillRect (r);
    }
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.strokePath (rounded (r.reduced (1.0f), R), juce::PathStrokeType (1.2f));
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.strokePath (shape, juce::PathStrokeType (1.0f));

    // Escalas dos knobs
    const float ox = r.getX() - 4.0f, oy = r.getY() - 6.0f;
    juce::ignoreUnused (ox, oy);
    for (int k = 0; k < numKnobs; ++k)
    {
        const auto kr = knobRect (k);
        const auto c = kr.getCentre();
        const float kR = kr.getWidth() * 0.5f;
        for (int t = 0; t <= 10; ++t)
        {
            const float a = knobStart + (knobEnd - knobStart) * (float) t / 10.0f;
            g.setColour (lk.ink.withAlpha (t % 5 == 0 ? 0.85f : 0.45f));
            g.drawLine (c.x + std::sin (a) * kR * 1.1f, c.y - std::cos (a) * kR * 1.1f,
                        c.x + std::sin (a) * kR * (t % 5 == 0 ? 1.24f : 1.18f), c.y - std::cos (a) * kR * (t % 5 == 0 ? 1.24f : 1.18f), 1.0f);
        }
        g.setColour (lk.ink);
        g.setFont (Fonts::condensedBold (juce::jmax (11.0f, W * 0.058f)));
        g.drawText (lk.labels[k], juce::Rectangle<float> (90.0f, 16.0f).withCentre ({ c.x, kr.getBottom() + juce::jmax (kR * 0.38f, 5.0f) + 4.0f }), juce::Justification::centred);
    }

    // Placa do nome
    const float nameY = r.getY() + H * 0.50f;
    switch (index)
    {
        case 0:
        {
            auto plate = juce::Rectangle<float> (r.getX() + W * 0.08f, nameY - H * 0.035f, W * 0.84f, H * 0.16f);
            g.setColour (juce::Colour (0xff0d120e));
            g.fillRoundedRectangle (plate, 4.0f);
            g.setColour (juce::Colour (0xffc9cdc6).withAlpha (0.8f));
            g.drawRoundedRectangle (plate.reduced (2.5f), 3.0f, 1.0f);
            g.setColour (lk.ink);
            g.setFont (Fonts::wide (plate.getHeight() * 0.3f));
            g.drawFittedText ("GREEN DRIVE", plate.reduced (8.0f, 0).withTrimmedBottom (plate.getHeight() * 0.35f).withTrimmedTop (plate.getHeight() * 0.1f).toNearestInt(), juce::Justification::centred, 1);
            g.setFont (Fonts::condensedBold (plate.getHeight() * 0.2f));
            g.setColour (lk.ink.withAlpha (0.7f));
            g.drawText (lk.subtitle, plate.withTrimmedTop (plate.getHeight() * 0.58f), juce::Justification::centred);
            break;
        }
        case 1:
        {
            auto plate = juce::Rectangle<float> (r.getX() + W * 0.08f, nameY - H * 0.08f, W * 0.84f, H * 0.17f);
            g.setColour (juce::Colour (0xff111111));
            g.fillRoundedRectangle (plate, 3.0f);
            auto f = Fonts::badge (plate.getHeight() * 0.36f);
            g.setFont (f);
            g.setColour (lk.top);
            g.drawText ("YELLOW", plate.withTrimmedBottom (plate.getHeight() * 0.4f).withTrimmedTop (plate.getHeight() * 0.05f), juce::Justification::centred);
            g.setColour (juce::Colours::white);
            g.setFont (Fonts::wide (plate.getHeight() * 0.15f));
            g.drawText ("D R I V E", plate.withTrimmedTop (plate.getHeight() * 0.56f).withTrimmedBottom (plate.getHeight() * 0.2f), juce::Justification::centred);
            g.setFont (Fonts::condensed (juce::jmax (9.0f, plate.getHeight() * 0.14f)));
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.drawText (lk.subtitle, plate.withTrimmedTop (plate.getHeight() * 0.8f), juce::Justification::centred);
            break;
        }
        case 2:
        {
            auto area = juce::Rectangle<float> (r.getX(), nameY - H * 0.085f, W, H * 0.13f);
            juce::GlyphArrangement ga;
            ga.addFittedText (Fonts::serif (area.getHeight() * 0.62f), "GOLD DRIVE", area.getX() + W * 0.06f, area.getY(),
                              W * 0.88f, area.getHeight(), juce::Justification::centred, 1);
            juce::Path letters; ga.createPath (letters);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.fillPath (letters, juce::AffineTransform::translation (0.0f, 1.2f));   // gravado no metal
            g.setColour (lk.ink);
            g.fillPath (letters);
            g.strokePath (letters, juce::PathStrokeType (0.6f));
            g.setColour (lk.ink.withAlpha (0.7f));
            g.fillRect (r.getX() + W * 0.2f, area.getBottom() + 2.0f, W * 0.6f, 1.0f);
            g.setFont (Fonts::condensed (juce::jmax (9.0f, H * 0.032f)));
            g.drawText (lk.subtitle, juce::Rectangle<float> (r.getX(), area.getBottom() + 5.0f, W, H * 0.05f), juce::Justification::centred);
            break;
        }
        case 4:
        {
            // Placa preta com letras prateadas (estilo compressor de estúdio)
            auto plate = juce::Rectangle<float> (r.getX() + W * 0.1f, nameY - H * 0.02f, W * 0.8f, H * 0.14f);
            g.setColour (juce::Colour (0xff141518));
            g.fillRoundedRectangle (plate, 3.0f);
            g.setColour (juce::Colours::white.withAlpha (0.3f));
            g.drawRoundedRectangle (plate.reduced (2.0f), 2.0f, 0.8f);
            g.setColour (juce::Colour (0xffdfe2e6));
            g.setFont (Fonts::wide (plate.getHeight() * 0.3f));
            g.drawFittedText ("SILVER", plate.withTrimmedBottom (plate.getHeight() * 0.42f).withTrimmedTop (plate.getHeight() * 0.08f).toNearestInt(), juce::Justification::centred, 1);
            g.setColour (juce::Colour (0xffe0453a));
            g.setFont (Fonts::condensedBold (plate.getHeight() * 0.26f));
            g.drawText ("C O M P", plate.withTrimmedTop (plate.getHeight() * 0.56f).withTrimmedBottom (plate.getHeight() * 0.08f), juce::Justification::centred);
            g.setColour (lk.ink.withAlpha (0.7f));
            g.setFont (Fonts::condensed (juce::jmax (9.0f, H * 0.032f)));
            g.drawText (lk.subtitle, juce::Rectangle<float> (r.getX(), plate.getBottom() + 3.0f, W, H * 0.05f), juce::Justification::centred);
            break;
        }
        case 3:
        default:
        {
            auto area = juce::Rectangle<float> (r.getX(), nameY - H * 0.08f, W, H * 0.16f);
            g.setFont (Fonts::wide (area.getHeight() * 0.44f));
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.drawText ("BLUE", area.translated (1.0f, 2.0f).withTrimmedBottom (area.getHeight() * 0.3f), juce::Justification::centred);
            g.setColour (lk.ink);
            g.drawText ("BLUE", area.withTrimmedBottom (area.getHeight() * 0.3f), juce::Justification::centred);
            g.setFont (Fonts::condensedBold (area.getHeight() * 0.3f));
            g.drawText ("D  R  I  V  E", area.withTrimmedTop (area.getHeight() * 0.62f), juce::Justification::centred);
            g.setColour (lk.ink.withAlpha (0.6f));
            g.setFont (Fonts::condensed (juce::jmax (9.0f, H * 0.032f)));
            g.drawText (lk.subtitle, juce::Rectangle<float> (r.getX(), area.getBottom() + 3.0f, W, H * 0.05f), juce::Justification::centred);
            break;
        }
    }

    // Tomadas no topo
    for (float fx : { 0.22f, 0.78f })
        jackSocket (g, { (float) getWidth() * fx, r.getY() + 8.0f }, 6.0f);

    // Parafusos da tampa
    for (auto pt : { r.getTopLeft().translated (9, 9), r.getTopRight().translated (-9, 9),
                     r.getBottomLeft().translated (9, -9), r.getBottomRight().translated (-9, -9) })
    {
        g.setGradientFill (chrome ({ pt.x - 3.0f, pt.y - 3.0f, 6.0f, 6.0f }));
        g.fillEllipse (pt.x - 3.0f, pt.y - 3.0f, 6.0f, 6.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawLine (pt.x - 2.0f, pt.y, pt.x + 2.0f, pt.y, 0.9f);
    }
}

void Stompbox::paint (juce::Graphics& g)
{
    if (body.isNull() || body.getWidth() != getWidth() * 2) renderBody();
    g.drawImage (body, getLocalBounds().toFloat());

    // LED (vermelho quando ligado)
    const juce::Point<float> c ((float) getWidth() * 0.5f, (float) getHeight() * (index == 0 ? 0.075f : 0.075f));
    const float r = 4.5f;
    const auto col = juce::Colour (0xffff2a1a);
    if (ledOn)
    {
        juce::ColourGradient glow (col.withAlpha (0.6f), c.x, c.y, col.withAlpha (0.0f), c.x + r * 5.0f, c.y, true);
        g.setGradientFill (glow);
        g.fillEllipse (c.x - r * 5.0f, c.y - r * 5.0f, r * 10.0f, r * 10.0f);
    }
    g.setColour (juce::Colour (0xff1a1a1a));
    g.fillEllipse (c.x - r - 1.5f, c.y - r - 1.5f, (r + 1.5f) * 2, (r + 1.5f) * 2);
    juce::ColourGradient lg (ledOn ? col.brighter (0.9f) : col.darker (0.85f), c.x - r * 0.3f, c.y - r * 0.4f,
                             ledOn ? col.darker (0.2f) : col.darker (0.95f), c.x + r, c.y + r, true);
    g.setGradientFill (lg);
    g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
}

void Stompbox::refresh()
{
    const bool on = state.getRawParameterValue (onParam (index))->load() > 0.5f;
    if (on != ledOn) { ledOn = on; repaint(); }
}

//==============================================================================
PedalsPage::PedalsPage (ValvedProcessor& p)
{
    // Ordem do sinal: compressor primeiro, depois os drives
    for (int look : { 4, 0, 1, 2, 3 })
        addAndMakeVisible (pedals.add (new Stompbox (p.apvts, look)));
}

void PedalsPage::refresh()
{
    for (auto* pd : pedals) pd->refresh();
}

void PedalsPage::resized()
{
    auto r = getLocalBounds().toFloat();
    board = r.reduced (34.0f, 0.0f).withTrimmedTop (64.0f).withTrimmedBottom (16.0f);
    const int n = pedals.size();
    const float gap = 22.0f;
    const float pw = juce::jmin (184.0f, (board.getWidth() - 40.0f - gap * (float) (n - 1)) / (float) n);
    const float ph = juce::jmin (372.0f, board.getHeight() - 62.0f);
    const float total = pw * (float) n + gap * (float) (n - 1);
    float x = board.getCentreX() - total * 0.5f;
    const float y = board.getY() + 44.0f;
    for (auto* pd : pedals)
    {
        pd->setBounds (juce::Rectangle<float> (x, y, pw, ph).toNearestInt());
        x += pw + gap;
    }
}

void PedalsPage::paint (juce::Graphics& g)
{
    // Texto de orientação
    g.setColour (textDim);
    g.setFont (Fonts::condensed (16.0f));
    g.drawText ("GUITAR  >  COMP  >  GREEN  >  YELLOW  >  GOLD  >  BLUE  >  AMP", getLocalBounds().removeFromTop (30).translated (0, 8), juce::Justification::centred);
    g.setColour (textDim.withAlpha (0.7f));
    g.setFont (Fonts::condensed (14.0f));
    g.drawText ("Tip: low drive + high level = clean boost that tightens and pushes the amp",
                getLocalBounds().removeFromTop (30).translated (0, 30), juce::Justification::centred);

    // Pedalboard: ripas de alumínio escuro
    const auto b = board;
    const auto bp = rounded (b, 14.0f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.8f), 30, { 0, 14 }).drawForPath (g, bp);
    g.setColour (juce::Colour (0xff141518));
    g.fillPath (bp);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (bp);
        const float slat = 52.0f;
        for (float y = b.getY() + 6.0f; y < b.getBottom(); y += slat)
        {
            auto s = juce::Rectangle<float> (b.getX(), y, b.getWidth(), slat - 12.0f);
            juce::ColourGradient sg (juce::Colour (0xff34363c), 0, s.getY(), juce::Colour (0xff1c1d21), 0, s.getBottom(), false);
            g.setGradientFill (sg);
            g.fillRect (s);
            g.setColour (juce::Colours::white.withAlpha (0.1f));
            g.fillRect (s.getX(), s.getY(), s.getWidth(), 1.0f);
            g.setColour (juce::Colours::black.withAlpha (0.8f));
            g.fillRect (s.getX(), s.getBottom(), s.getWidth(), 2.0f);
        }
    }
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.strokePath (rounded (b.reduced (0.5f), 14.0f), juce::PathStrokeType (1.0f));
}

void PedalsPage::paintOverChildren (juce::Graphics& g)
{
    auto cable = [&g] (juce::Point<float> a, juce::Point<float> b, float lift)
    {
        juce::Path p;
        p.startNewSubPath (a);
        p.cubicTo (a.translated (0, -lift), b.translated (0, -lift), b);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.strokePath (p, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), juce::AffineTransform::translation (0, 4));
        g.setColour (juce::Colour (0xff141414));
        g.strokePath (p, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.strokePath (p, juce::PathStrokeType (1.2f), juce::AffineTransform::translation (-1.0f, -1.5f));
    };
    auto plug = [&g] (juce::Point<float> c)
    {
        auto body = juce::Rectangle<float> (12.0f, 20.0f).withCentre (c.translated (0, -8.0f));
        g.setGradientFill (chrome (body));
        g.fillRoundedRectangle (body, 3.0f);
        g.setColour (juce::Colour (0xff111111));
        g.fillRoundedRectangle (body.withTrimmedTop (body.getHeight() * 0.55f).reduced (1.0f, 0), 2.0f);
    };

    for (int i = 0; i + 1 < pedals.size(); ++i)
    {
        const auto a = pedals[i]->jackOut().translated (0, -18.0f), b = pedals[i + 1]->jackIn().translated (0, -18.0f);
        cable (a, b, 36.0f);
        plug (pedals[i]->jackOut().translated (0, 6.0f));
        plug (pedals[i + 1]->jackIn().translated (0, 6.0f));
    }
    // Guitarra e amp
    const auto in = pedals.getFirst()->jackIn().translated (0, -18.0f);
    const auto out = pedals.getLast()->jackOut().translated (0, -18.0f);
    cable ({ board.getX() + 4.0f, in.y - 12.0f }, in, 20.0f);
    cable (out, { board.getRight() - 4.0f, out.y - 12.0f }, 20.0f);
    plug (pedals.getFirst()->jackIn().translated (0, 6.0f));
    plug (pedals.getLast()->jackOut().translated (0, 6.0f));

    g.setFont (Fonts::condensedBold (13.0f));
    g.setColour (textDim);
    g.drawText ("GUITAR", juce::Rectangle<float> (board.getX() + 10.0f, in.y - 40.0f, 80.0f, 16.0f), juce::Justification::centredLeft);
    g.drawText ("TO AMP", juce::Rectangle<float> (board.getRight() - 90.0f, out.y - 40.0f, 80.0f, 16.0f), juce::Justification::centredRight);
}

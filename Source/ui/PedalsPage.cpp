#include "PedalsPage.h"
#include "../PluginProcessor.h"

using namespace ui;

namespace
{
    // Pedais em imagem (UI kit "PRE FX"). Coordenadas em px da foto original (941 x 1672).
    struct KnobSpot { float x, y, box; int sprite; };
    struct PedalSpec
    {
        const void* body; int bodySize;          // foto do pedal (recortada, metade da resolução)
        const void* led;  int ledSize;           // LED apagado
        float bx, by, bw, bh;                    // recorte útil na foto original
        float ledX, ledY, ledR;
        int numKnobs;
        KnobSpot knobs[4];                       // na ordem dos parâmetros
        float footX, footY, footD;
        float slotX, slotY, slotW, slotH;        // chave deslizante (0 = não tem)
        const char* tips[4];
    };

    const PedalSpec& spec (int look)
    {
        using namespace BinaryData;
        static const PedalSpec green  { pedal_green_png, pedal_green_pngSize, pedal_green_ledoff_png, pedal_green_ledoff_pngSize,
            67, 44, 805, 1567, 470, 226, 26, 3,
            { { 293.5f, 381.0f, 179, 0 }, { 470.4f, 630.7f, 126, 1 }, { 646.5f, 381.0f, 179, 0 } },
            470, 1344, 200, 403, 1080, 134, 53, { "Drive", "Tone", "Level" } };
        static const PedalSpec yellow { pedal_yellow_png, pedal_yellow_pngSize, pedal_yellow_ledoff_png, pedal_yellow_ledoff_pngSize,
            66, 88, 805, 1509, 469, 308, 30, 3,
            { { 704.7f, 468.8f, 139, 4 }, { 470.0f, 468.8f, 139, 4 }, { 234.8f, 469.5f, 139, 4 } },
            470, 1305, 210, 0, 0, 0, 0, { "Drive", "Tone", "Level" } };
        static const PedalSpec gold   { pedal_gold_png, pedal_gold_pngSize, pedal_gold_ledoff_png, pedal_gold_ledoff_pngSize,
            46, 88, 848, 1524, 471, 301, 29, 3,
            { { 227.6f, 512.0f, 147, 5 }, { 469.7f, 511.0f, 147, 5 }, { 710.7f, 512.0f, 147, 5 } },
            470, 1328, 170, 0, 0, 0, 0, { "Gain", "Treble", "Output" } };
        static const PedalSpec blue   { pedal_blue_png, pedal_blue_pngSize, pedal_blue_ledoff_png, pedal_blue_ledoff_pngSize,
            33, 29, 875, 1613, 470, 328, 33, 3,
            { { 470.4f, 495.0f, 152, 6 }, { 720.3f, 495.0f, 152, 6 }, { 220.3f, 495.0f, 152, 6 } },
            470, 1351, 170, 0, 0, 0, 0, { "Drive", "Tone", "Volume" } };
        static const PedalSpec chorus { pedal_chorus_png, pedal_chorus_pngSize, pedal_chorus_ledoff_png, pedal_chorus_ledoff_pngSize,
            87, 32, 766, 1586, 470, 230, 30, 4,
            { { 293.9f, 350.8f, 164, 2 }, { 646.2f, 351.7f, 164, 2 }, { 299.7f, 630.0f, 109, 3 }, { 649.7f, 630.0f, 109, 3 } },
            470, 1365, 190, 366, 1116, 200, 72, { "Level", "Intensity", "Vib Depth", "Vib Rate" } };
        switch (look) { case 0: return green; case 1: return yellow; case 2: return gold; case 3: return blue; default: return chorus; }
    }

    bool isChorus (int look) { return look == 5; }
    bool isComp (int look) { return look == 4; }

    juce::String knobParam (int look, int k)
    {
        if (look == 5)
        {
            static const char* ids[4] = { ParamID::ceLevel, ParamID::ceIntensity, ParamID::ceDepth, ParamID::ceRate };
            return ids[k];
        }
        if (isComp (look))
        {
            static const char* ids[4] = { ParamID::compSustain, ParamID::compLevel, ParamID::compAttack, ParamID::compClip };
            return ids[k];
        }
        static const char* suffix[3] = { "Drive", "Tone", "Level" };
        return ParamID::pedal (look, suffix[k]);
    }

    juce::String onParam (int look)
    {
        if (look == 5) return ParamID::ceOn;
        return isComp (look) ? juce::String (ParamID::compOn) : ParamID::pedal (look, "On");
    }

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
void SlideSwitch::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    static const juce::Image slot = imgui::load (BinaryData::slot_chorus_png, BinaryData::slot_chorus_pngSize);
    static const juce::Image thumb = imgui::load (BinaryData::slot_thumb_png, BinaryData::slot_thumb_pngSize);
    const auto r = getLocalBounds().toFloat();
    if (drawSlot) imgui::draw (g, slot, r);
    // a bolinha ocupa 112 x 72 do trilho de 200 x 72 (proporção da foto do Ensemble)
    const float tw = r.getHeight() * 112.0f / 72.0f;
    const bool on = getToggleState();
    auto t = juce::Rectangle<float> (tw, r.getHeight()).withX (on ? r.getRight() - tw : r.getX()).withY (r.getY());
    if (on)
    {
        // espelha (a luz da foto vem de cima, então fica natural)
        juce::Graphics::ScopedSaveState ss (g);
        g.addTransform (juce::AffineTransform::scale (-1.0f, 1.0f, t.getCentreX(), 0.0f));
        imgui::draw (g, thumb, t);
    }
    else imgui::draw (g, thumb, t);
    if (highlighted) { g.setColour (juce::Colours::white.withAlpha (0.07f)); g.fillRoundedRectangle (t.reduced (t.getHeight() * 0.12f), t.getHeight() * 0.3f); }
}

//==============================================================================
Stompbox::Stompbox (juce::AudioProcessorValueTreeState& s, int pedalIndex) : state (s), index (pedalIndex)
{
    const auto& sp = spec (index);
    body = imgui::load (sp.body, sp.bodySize);
    ledOff = imgui::load (sp.led, sp.ledSize);
    numKnobs = sp.numKnobs;
    for (int k = 0; k < numKnobs; ++k)
    {
        auto& kn = knobs[k];
        kn.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        kn.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        kn.setRotaryParameters (knobStart, knobEnd, true);
        kn.setPopupDisplayEnabled (true, true, nullptr);
        kn.setMouseDragSensitivity (200);
        kn.getProperties().set ("knobStyle", (int) KnobSprite);
        kn.getProperties().set ("sprite", sp.knobs[k].sprite);
        kn.setTooltip (sp.tips[k]);
        addAndMakeVisible (kn);
        const auto id = knobParam (index, k);
        knobAtt[k] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, kn);
        if (auto* prm = s.getParameter (id))
            kn.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    }
    addAndMakeVisible (footswitch);
    footswitch.setTooltip ("On / Off");
    switchAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, onParam (index), footswitch);

    if (index == 0 || isChorus (index))
    {
        modeSwitch = std::make_unique<SlideSwitch>();
        modeSwitch->drawSlot = isChorus (index);
        modeSwitch->setTooltip (index == 0 ? "TS808 or TS9 circuit (output stage / op-amp)" : "Chorus (INTENSITY) or Vibrato (VIB DEPTH / VIB RATE)");
        addAndMakeVisible (*modeSwitch);
        modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, index == 0 ? ParamID::greenMode : ParamID::ceMode, *modeSwitch);
    }
}

float Stompbox::nativeWidth() const  { return spec (index).bw; }
float Stompbox::nativeHeight() const { return spec (index).bh; }

imgui::Mapper Stompbox::mapper() const
{
    const auto& sp = spec (index);
    return { getLocalBounds().toFloat(), sp.bx, sp.by, sp.bw, sp.bh };
}

void Stompbox::resized()
{
    const auto& sp = spec (index);
    const auto m = mapper();
    for (int k = 0; k < numKnobs; ++k)
        knobs[k].setBounds (m.box (sp.knobs[k].x, sp.knobs[k].y, sp.knobs[k].box, sp.knobs[k].box).toNearestInt());
    if (modeSwitch != nullptr)
    {
        if (index == 0)   // trilho do Green (só a bolinha é desenhada, um pouco menor que o trilho)
            modeSwitch->setBounds (m.rect (sp.slotX + 6.0f, sp.slotY + 5.0f, sp.slotW - 12.0f, sp.slotH - 10.0f).toNearestInt());
        else
            modeSwitch->setBounds (m.rect (sp.slotX, sp.slotY, sp.slotW, sp.slotH).toNearestInt());
    }
    footswitch.setBounds (m.box (sp.footX, sp.footY, sp.footD, sp.footD).toNearestInt());
}

juce::Point<float> Stompbox::jackIn() const  { return getBounds().toFloat().getTopLeft().translated ((float) getWidth() * 0.22f, 2.0f); }
juce::Point<float> Stompbox::jackOut() const { return getBounds().toFloat().getTopLeft().translated ((float) getWidth() * 0.78f, 2.0f); }

void Stompbox::paint (juce::Graphics& g)
{
    const auto& sp = spec (index);
    const auto m = mapper();
    // sombra do pedal na pedalboard
    {
        const auto r = getLocalBounds().toFloat().reduced ((float) getWidth() * 0.04f, (float) getHeight() * 0.02f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.75f), 20, { 0, 10 }).drawForPath (g, rounded (r, r.getWidth() * 0.08f));
    }
    imgui::draw (g, body, getLocalBounds().toFloat());
    if (! ledOn)
        imgui::draw (g, ledOff, m.box (sp.ledX, sp.ledY, sp.ledR * 2.0f, sp.ledR * 2.0f));
}

void Stompbox::refresh()
{
    const bool on = state.getRawParameterValue (onParam (index))->load() > 0.5f;
    if (on != ledOn) { ledOn = on; repaint(); }
}

//==============================================================================
PedalsPage::PedalsPage (ValvedProcessor& p)
{
    // Ordem do sinal: chorus primeiro, depois os drives (o Silver Comp está guardado, fora da pedalboard)
    for (int look : { 5, 0, 1, 2, 3 })
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
    const float gap = 20.0f;
    float sumW = 0.0f, maxH = 0.0f;
    for (auto* pd : pedals) { sumW += pd->nativeWidth(); maxH = juce::jmax (maxH, pd->nativeHeight()); }
    // todos na mesma escala (o tamanho relativo dos pedais é o das fotos)
    const float sc = juce::jmin ((board.getWidth() - 40.0f - gap * (float) (pedals.size() - 1)) / sumW, (board.getHeight() - 58.0f) / maxH);
    float x = board.getCentreX() - (sumW * sc + gap * (float) (pedals.size() - 1)) * 0.5f;
    const float bottom = board.getY() + 44.0f + maxH * sc;
    for (auto* pd : pedals)
    {
        const float w = pd->nativeWidth() * sc, h = pd->nativeHeight() * sc;
        pd->setBounds (juce::Rectangle<float> (x, bottom - h - (maxH * sc - h) * 0.5f, w, h).toNearestInt());
        x += w + gap;
    }
}

void PedalsPage::paint (juce::Graphics& g)
{
    // Texto de orientação
    g.setColour (textDim);
    g.setFont (Fonts::condensed (16.0f));
    g.drawText ("GUITAR  >  CHORUS  >  GREEN  >  YELLOW  >  GOLD  >  BLUE  >  AMP", getLocalBounds().removeFromTop (30).translated (0, 8), juce::Justification::centred);
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

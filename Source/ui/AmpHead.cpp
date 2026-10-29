#include "AmpHead.h"
#include "../PluginProcessor.h"

using namespace ui;

namespace
{
    enum { Clean = 0, Brit = 1, Block = 2, Rec = 3 };

    const std::vector<AmpHead::Control> controlsOf[4] = {
        { { ParamID::gain, "VOLUME", false, 0 },   { ParamID::treble, "TREBLE", false, 0 },
          { ParamID::mid, "MIDDLE", false, 0 },    { ParamID::bass, "BASS", false, 0 },
          { ParamID::presence, "PRESENCE", false, 1 }, { ParamID::master, "MASTER", false, 1 },
          { ParamID::chorus, "ON / OFF", true, 2 }, { ParamID::chorusRate, "RATE", false, 2 },
          { ParamID::chorusDepth, "DEPTH", false, 2 } },
        { { ParamID::gain, "GAIN", false, 0 },     { ParamID::bass, "BASS", false, 0 },
          { ParamID::mid, "MIDDLE", false, 0 },    { ParamID::treble, "TREBLE", false, 0 },
          { ParamID::presence, "PRESENCE", false, 1 }, { ParamID::resonance, "RESONANCE", false, 1 },
          { ParamID::master, "MASTER", false, 2 }, { ParamID::britMode, "80'S / MODERN", true, 3 } },
        { { ParamID::gain, "PRE GAIN", false, 0 }, { ParamID::bass, "LOW", false, 0 },
          { ParamID::mid, "MID", false, 0 },       { ParamID::treble, "HIGH", false, 0 },
          { ParamID::master, "POST GAIN", false, 1 }, { ParamID::resonance, "RESONANCE", false, 1 },
          { ParamID::presence, "PRESENCE", false, 1 } },
        { { ParamID::gain, "GAIN", false, 0 },     { ParamID::treble, "TREBLE", false, 0 },
          { ParamID::mid, "MID", false, 0 },       { ParamID::bass, "BASS", false, 0 },
          { ParamID::presence, "PRESENCE", false, 1 }, { ParamID::master, "MASTER", false, 1 },
          { ParamID::recMode, "VINTAGE / MODERN", true, 2 } },
    };

    const std::vector<const char*> sectionTitles[4] = {
        { "PREAMP", "OUTPUT", "CHORUS" },
        { "PREAMP", "POWER AMP", "MASTER", "MODE" },
        { "PREAMP", "POWER AMP" },
        { "PREAMP", "MASTER", "MODE" },
    };

    // Faixa horizontal (fração da largura do painel) onde ficam os controles
    const float rangeOf[4][2] = { { 0.065f, 0.700f }, { 0.205f, 0.885f }, { 0.155f, 0.795f }, { 0.135f, 0.800f } };

    const int knobStyleOf[4] = { KnobBlackPointer, KnobBrass, KnobBlackPointer, KnobBlackChrome };

    constexpr float knobStart = juce::MathConstants<float>::pi * (7.0f / 6.0f);
    constexpr float knobEnd   = juce::MathConstants<float>::pi * (17.0f / 6.0f);

    //--------------------------------------------------------------------------
    juce::Path rounded (juce::Rectangle<float> r, float radius)
    {
        juce::Path p;
        p.addRoundedRectangle (r, radius);
        return p;
    }

    void dropShadow (juce::Graphics& g, const juce::Path& shape, float radius, juce::Point<int> offset, float alpha)
    {
        juce::DropShadow (juce::Colours::black.withAlpha (alpha), (int) radius, offset).drawForPath (g, shape);
    }

    // Couro/tolex: granulado fino + "pedrinhas"
    void leather (juce::Graphics& g, juce::Rectangle<float> r, int seed, float strength = 1.0f)
    {
        juce::Random rng (seed);
        const int n = (int) (r.getWidth() * r.getHeight() * 0.22f);
        for (int i = 0; i < n; ++i)
        {
            const float x = r.getX() + rng.nextFloat() * r.getWidth();
            const float y = r.getY() + rng.nextFloat() * r.getHeight();
            const float s = 1.2f + rng.nextFloat() * 2.6f;
            const bool light = rng.nextFloat() < 0.45f;
            g.setColour ((light ? juce::Colours::white : juce::Colours::black).withAlpha ((light ? 0.035f : 0.12f) * strength * rng.nextFloat()));
            g.fillEllipse (x, y, s, s * (0.6f + rng.nextFloat() * 0.6f));
        }
        const int m = (int) (r.getWidth() * r.getHeight() * 0.5f);
        for (int i = 0; i < m; ++i)
        {
            g.setColour ((rng.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.05f * strength * rng.nextFloat()));
            g.fillRect (r.getX() + rng.nextFloat() * r.getWidth(), r.getY() + rng.nextFloat() * r.getHeight(), 1.0f, 1.0f);
        }
    }

    void brushed (juce::Graphics& g, juce::Rectangle<float> r, int seed, juce::Colour c, float alpha)
    {
        juce::Random rng (seed);
        for (float y = r.getY(); y < r.getBottom(); y += 0.75f)
        {
            g.setColour (c.withAlpha (alpha * rng.nextFloat()));
            const float x0 = r.getX() + rng.nextFloat() * r.getWidth() * 0.3f;
            g.fillRect (x0, y, r.getWidth() * (0.4f + rng.nextFloat() * 0.6f), 0.5f);
        }
    }

    juce::ColourGradient steel (juce::Rectangle<float> r, juce::Colour light, juce::Colour dark)
    {
        juce::ColourGradient cg (light, r.getX(), r.getY(), dark, r.getX(), r.getBottom(), false);
        cg.addColour (0.45, light.interpolatedWith (dark, 0.35f));
        cg.addColour (0.55, dark.interpolatedWith (light, 0.15f));
        return cg;
    }

    void screw (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour tone = juce::Colour (0xff8a8c90))
    {
        juce::ColourGradient sg (tone.brighter (0.6f), c.x - r, c.y - r, tone.darker (0.8f), c.x + r, c.y + r, false);
        g.setGradientFill (sg);
        g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawLine (c.x - r * 0.6f, c.y - r * 0.2f, c.x + r * 0.6f, c.y + r * 0.2f, juce::jmax (0.8f, r * 0.28f));
    }

    void jack (juce::Graphics& g, juce::Point<float> c, float r)
    {
        juce::Path p; p.addEllipse (c.x - r, c.y - r, r * 2, r * 2);
        dropShadow (g, p, 5, { 0, 2 }, 0.6f);
        juce::ColourGradient ng (juce::Colour (0xffe6e6e6), c.x - r, c.y - r, juce::Colour (0xff3c3e43), c.x + r, c.y + r, false);
        g.setGradientFill (ng);
        g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
        g.setColour (juce::Colour (0xff060606));
        g.fillEllipse (c.x - r * 0.62f, c.y - r * 0.62f, r * 1.24f, r * 1.24f);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawEllipse (c.x - r * 0.62f, c.y - r * 0.62f, r * 1.24f, r * 1.24f, 0.8f);
    }

    void rocker (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour body, bool redMark)
    {
        const auto bez = rounded (r, r.getWidth() * 0.12f);
        dropShadow (g, bez, 5, { 0, 2 }, 0.7f);
        g.setColour (juce::Colour (0xff050505));
        g.fillPath (bez);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.strokePath (bez, juce::PathStrokeType (0.8f));
        auto in = r.reduced (r.getWidth() * 0.16f);
        juce::ColourGradient rg (body.brighter (0.45f), in.getX(), in.getY(), body.darker (0.55f), in.getX(), in.getBottom(), false);
        rg.addColour (0.48, body);
        rg.addColour (0.52, body.darker (0.35f));
        g.setGradientFill (rg);
        g.fillRoundedRectangle (in, in.getWidth() * 0.1f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.fillRect (in.getX() + 1.0f, in.getY() + 1.0f, in.getWidth() - 2.0f, 1.0f);
        g.setColour (redMark ? juce::Colour (0xffff4a3a) : juce::Colours::white.withAlpha (0.6f));
        g.fillRect (in.getCentreX() - 0.8f, in.getY() + in.getHeight() * 0.18f, 1.6f, in.getHeight() * 0.25f);
    }

    void toggleLever (juce::Graphics& g, juce::Point<float> c, float r)
    {
        juce::ColourGradient ng (juce::Colour (0xffe8e8e8), c.x - r, c.y - r, juce::Colour (0xff45474c), c.x + r, c.y + r, false);
        g.setGradientFill (ng);
        g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
        g.setColour (juce::Colour (0xff111111));
        g.fillEllipse (c.x - r * 0.55f, c.y - r * 0.55f, r * 1.1f, r * 1.1f);
        const float len = r * 1.8f;
        juce::Path lever;
        lever.startNewSubPath (c.x - r * 0.25f, c.y);
        lever.lineTo (c.x + r * 0.25f, c.y);
        lever.lineTo (c.x + r * 0.34f, c.y - len);
        lever.lineTo (c.x - r * 0.34f, c.y - len);
        lever.closeSubPath();
        juce::ColourGradient lg (juce::Colour (0xff6d7076), c.x - r * 0.4f, c.y, juce::Colour (0xffffffff), c.x + r * 0.1f, c.y, false);
        g.setGradientFill (lg);
        g.fillPath (lever);
        g.fillEllipse (c.x - r * 0.4f, c.y - len - r * 0.4f, r * 0.8f, r * 0.8f);
    }

    void led (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col, bool lit = true)
    {
        if (lit)
        {
            juce::ColourGradient glow (col.withAlpha (0.55f), c.x, c.y, col.withAlpha (0.0f), c.x + r * 4.0f, c.y, true);
            g.setGradientFill (glow);
            g.fillEllipse (c.x - r * 4.0f, c.y - r * 4.0f, r * 8.0f, r * 8.0f);
        }
        juce::ColourGradient lg (lit ? col.brighter (0.9f) : col.darker (0.8f), c.x - r * 0.3f, c.y - r * 0.4f,
                                 lit ? col.darker (0.3f) : col.darker (0.95f), c.x + r, c.y + r, true);
        g.setGradientFill (lg);
        g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
    }

    void drawTxt (juce::Graphics& g, const juce::String& t, juce::Point<float> c, juce::Font f, juce::Colour col, float w = 160.0f)
    {
        g.setColour (col);
        g.setFont (f);
        g.drawText (t, juce::Rectangle<float> (w, f.getHeight() + 4.0f).withCentre (c), juce::Justification::centred);
    }

    // Título de seção com linhas dos dois lados
    void sectionTitle (juce::Graphics& g, const juce::String& t, float x0, float x1, float y, juce::Font f,
                       juce::Colour col, juce::Colour lineCol)
    {
        juce::GlyphArrangement ga; ga.addLineOfText (f, t, 0, 0);
        const float tw = ga.getBoundingBox (0, -1, true).getWidth() + 14.0f;
        const float cx = (x0 + x1) * 0.5f;
        g.setColour (lineCol);
        g.fillRect (x0, y, juce::jmax (0.0f, cx - tw * 0.5f - x0), 1.0f);
        g.fillRect (cx + tw * 0.5f, y, juce::jmax (0.0f, x1 - cx - tw * 0.5f), 1.0f);
        drawTxt (g, t, { cx, y + 0.5f }, f, col, tw + 20.0f);
    }
}

// Knobs com regulagem própria em cada amp têm o id "aN_knob"
juce::String AmpHead::fullId (int m, const char* base)
{
    for (auto* k : { ParamID::gain, ParamID::bass, ParamID::mid, ParamID::treble, ParamID::presence, ParamID::master, ParamID::resonance })
        if (juce::String (base) == k) return ParamID::ampKnob (m, base);
    return base;
}

//==============================================================================
AmpHead::AmpHead (juce::AudioProcessorValueTreeState& s) : state (s)
{
    for (int m = 0; m < 4; ++m)
        for (auto c : controlsOf[m])
        {
            const juce::String id = fullId (m, c.paramId);
            if (c.isSwitch)
            {
                if (switchIds.contains (id)) continue;
                const bool rec = id == ParamID::recMode || id == ParamID::britMode;
                auto* sw = switches.add (new ToggleSwitch (rec ? juce::Colour (0xffff4a2a) : juce::Colour (0xff36d1ff)));
                switchIds.add (id);
                addChildComponent (sw);
                switchAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, id, *sw));
            }
            else
            {
                if (sliderIds.contains (id)) continue;
                auto* k = sliders.add (new juce::Slider());
                k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
                k->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
                k->setRotaryParameters (knobStart, knobEnd, true);
                k->setPopupDisplayEnabled (true, true, this);
                k->setMouseDragSensitivity (220);
                sliderIds.add (id);
                addChildComponent (k);
                sliderAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, *k));
                if (auto* prm = s.getParameter (id))
                    k->setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
            }
        }
    model = -1;
    setModel (1);
}

AmpHead::~AmpHead() = default;

juce::Slider* AmpHead::findSlider (const juce::String& id)
{
    const int i = sliderIds.indexOf (id);
    return i >= 0 ? sliders[i] : nullptr;
}

ToggleSwitch* AmpHead::findSwitch (const juce::String& id)
{
    const int i = switchIds.indexOf (id);
    return i >= 0 ? switches[i] : nullptr;
}

const std::vector<AmpHead::Control>& AmpHead::controls() const { return controlsOf[juce::jlimit (0, 3, model)]; }

void AmpHead::setModel (int m)
{
    m = juce::jlimit (0, 3, m);
    if (m == model) return;
    model = m;
    for (auto* k : sliders) { k->setVisible (false); k->getProperties().set ("knobStyle", knobStyleOf[model]); }
    for (auto* sw : switches) sw->setVisible (false);
    for (auto& c : controls())
    {
        if (c.isSwitch) { if (auto* sw = findSwitch (fullId (model, c.paramId))) sw->setVisible (true); }
        else if (auto* k = findSlider (fullId (model, c.paramId))) k->setVisible (true);
    }
    layoutControls();
    refreshState();
    cacheModel = -1;
    repaint();
}

void AmpHead::refreshState()
{
    const bool chorusOn = state.getRawParameterValue (ParamID::chorus)->load() > 0.5f;
    for (auto* id : { ParamID::chorusRate, ParamID::chorusDepth })
        if (auto* k = findSlider (id))
        {
            k->setEnabled (chorusOn);
            k->setAlpha (chorusOn ? 1.0f : 0.4f);
        }
}

AmpHead::Geometry AmpHead::computeGeometry() const
{
    Geometry geo;
    const float W = (float) getWidth(), H = (float) getHeight();
    const float aspect = 2.4f;
    const float bw = juce::jmin (W - 40.0f, (H - 30.0f) / (1.0f / aspect + 0.07f));
    const float bh = bw / aspect;
    const float x = (W - bw) * 0.5f;
    const float y = (H - bh * 1.07f) * 0.5f + bh * 0.05f;
    geo.body = { x, y, bw, bh };
    geo.handle = { x + bw * 0.20f, y - bh * 0.06f, bw * 0.60f, bh * 0.07f };

    geo.front = { x + bh * 0.075f, y + bh * 0.11f, bw - bh * 0.15f, bh * (1.0f - 0.11f - 0.085f) };

    auto f = geo.front;
    switch (model)
    {
        case Clean: geo.panel = f.removeFromTop (f.getHeight() * 0.29f); f.removeFromTop (bh * 0.018f); geo.grille = f; break;
        case Brit:  geo.grille = f.removeFromTop (f.getHeight() * 0.60f); geo.panel = f; break;
        case Block: geo.grille = f.removeFromTop (f.getHeight() * 0.60f); f.removeFromTop (bh * 0.015f); geo.panel = f; break;
        case Rec:
        default:    geo.grille = f; geo.panel = f.withTrimmedTop (f.getHeight() * 0.66f); break;
    }

    const auto& p = geo.panel;
    const auto& list = controls();
    const float x0 = p.getX() + p.getWidth() * rangeOf[model][0];
    const float x1 = p.getX() + p.getWidth() * rangeOf[model][1];
    float units = 0;
    for (size_t i = 1; i < list.size(); ++i)
        units += (list[i].group != list[i - 1].group) ? 1.45f : 1.0f;
    const float step = (x1 - x0) / juce::jmax (1.0f, units);
    geo.knobD = juce::jmin (p.getHeight() * 0.36f, step * 0.58f);
    const float ky = p.getY() + p.getHeight() * 0.53f;
    geo.titleY = p.getY() + p.getHeight() * 0.15f;
    geo.labelY = p.getY() + p.getHeight() * 0.86f;
    float cx = x0;
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (i > 0) cx += step * ((list[i].group != list[i - 1].group) ? 1.45f : 1.0f);
        geo.pos.push_back ({ cx, ky });
    }
    return geo;
}

void AmpHead::layoutControls()
{
    if (getWidth() <= 0) return;
    const auto geo = computeGeometry();
    const auto& list = controls();
    for (size_t i = 0; i < list.size(); ++i)
    {
        const auto c = geo.pos[i];
        if (list[i].isSwitch)
        {
            if (auto* sw = findSwitch (fullId (model, list[i].paramId)))
            {
                const float s = geo.knobD * 0.95f;
                sw->setBounds (juce::Rectangle<float> (s, s * 1.3f).withCentre (c.translated (0, -s * 0.05f)).toNearestInt());
            }
        }
        else if (auto* k = findSlider (fullId (model, list[i].paramId)))
            k->setBounds (juce::Rectangle<float> (geo.knobD, geo.knobD).withCentre (c).toNearestInt());
    }
}

void AmpHead::resized()
{
    layoutControls();
    cacheModel = -1;
}

void AmpHead::paint (juce::Graphics& g)
{
    if (cacheModel != model || cacheBounds != getLocalBounds())
        renderBackground();
    g.drawImage (cache, getLocalBounds().toFloat());
}

void AmpHead::renderBackground()
{
    cacheModel = model;
    cacheBounds = getLocalBounds();
    const float scale = 2.0f;
    cache = juce::Image (juce::Image::ARGB, juce::jmax (1, (int) (getWidth() * scale)),
                         juce::jmax (1, (int) (getHeight() * scale)), true);
    juce::Graphics g (cache);
    g.addTransform (juce::AffineTransform::scale (scale));

    const auto geo = computeGeometry();
    drawCabinet (g, geo);
    switch (model)
    {
        case Clean: drawClean (g, geo); break;
        case Brit:  drawBrit (g, geo); break;
        case Block: drawBlock (g, geo); break;
        case Rec:
        default:    drawRec (g, geo); break;
    }
}

//==============================================================================
// Caixa (tolex, alça, cantoneiras) — comum aos quatro, com detalhes por modelo
void AmpHead::drawCabinet (juce::Graphics& g, const Geometry& geo)
{
    const auto b = geo.body;
    const float bh = b.getHeight();

    // Sombra no chão
    {
        auto floor = juce::Rectangle<float> (b.getX() + b.getWidth() * 0.03f, b.getBottom() - 8.0f, b.getWidth() * 0.94f, 30.0f);
        juce::ColourGradient fg (juce::Colours::black.withAlpha (0.85f), floor.getCentreX(), floor.getCentreY(),
                                 juce::Colours::transparentBlack, floor.getX(), floor.getCentreY(), true);
        g.setGradientFill (fg);
        g.fillEllipse (floor);
    }
    // Pés
    for (float fx : { 0.07f, 0.93f })
    {
        auto foot = juce::Rectangle<float> (b.getWidth() * 0.06f, bh * 0.04f).withCentre ({ b.getX() + b.getWidth() * fx, b.getBottom() + bh * 0.01f });
        g.setColour (juce::Colour (0xff070707));
        g.fillRoundedRectangle (foot, 3.0f);
    }

    // Alça: placas de metal + alça preta
    {
        const auto h = geo.handle;
        const float plateW = b.getWidth() * 0.075f, plateH = bh * 0.028f;
        const juce::Colour metal = model == Clean ? juce::Colour (0xff9a9da2) : juce::Colour (0xff3a3b3f);
        for (float px : { h.getX(), h.getRight() - plateW })
        {
            auto plate = juce::Rectangle<float> (px, b.getY() - plateH * 0.75f, plateW, plateH);
            g.setGradientFill (steel (plate, metal.brighter (0.4f), metal.darker (0.6f)));
            g.fillRoundedRectangle (plate, 2.0f);
        }
        auto strap = juce::Rectangle<float> (h.getX() + plateW * 0.55f, h.getY(), h.getWidth() - plateW * 1.1f, bh * 0.032f);
        juce::Path sp;
        sp.addRoundedRectangle (strap, strap.getHeight() * 0.5f);
        dropShadow (g, sp, 6, { 0, 3 }, 0.8f);
        juce::ColourGradient lg (juce::Colour (0xff3a393a), 0, strap.getY(), juce::Colour (0xff050505), 0, strap.getBottom(), false);
        g.setGradientFill (lg);
        g.fillPath (sp);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRect (strap.getX() + strap.getHeight(), strap.getY() + 1.5f, strap.getWidth() - strap.getHeight() * 2, 1.0f);
        for (float ex : { strap.getX(), strap.getRight() })
        {
            g.setColour (juce::Colour (0xff0c0c0c));
            g.fillRoundedRectangle (juce::Rectangle<float> (strap.getHeight() * 1.2f, b.getY() - strap.getY() + 2.0f)
                                        .withCentre ({ ex, (strap.getY() + b.getY()) * 0.5f + 1.0f }), 3.0f);
        }
    }

    // Corpo
    const float R = bh * 0.035f;
    const auto bodyPath = rounded (b, R);
    dropShadow (g, bodyPath, 30, { 0, 14 }, 0.9f);
    juce::ColourGradient tg (juce::Colour (0xff222224), b.getX(), b.getY(), juce::Colour (0xff0a0a0b), b.getX(), b.getBottom(), false);
    g.setGradientFill (tg);
    g.fillPath (bodyPath);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (bodyPath);
        leather (g, b, 21 + model, 1.0f);
        juce::ColourGradient top (juce::Colours::white.withAlpha (0.10f), 0, b.getY(), juce::Colours::transparentWhite, 0, b.getY() + bh * 0.2f, false);
        g.setGradientFill (top);
        g.fillRect (b);
        juce::ColourGradient side (juce::Colours::black.withAlpha (0.4f), b.getX(), 0, juce::Colours::black.withAlpha (0.4f), b.getRight(), 0, false);
        side.addColour (0.08, juce::Colours::transparentBlack);
        side.addColour (0.92, juce::Colours::transparentBlack);
        g.setGradientFill (side);
        g.fillRect (b);
    }
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.strokePath (rounded (b.reduced (0.5f), R), juce::PathStrokeType (1.0f));

    // Rebaixo da frente (borda interna do tolex)
    g.setColour (juce::Colour (0xff040405));
    g.fillPath (rounded (geo.front.expanded (2.0f), 3.0f));
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.strokePath (rounded (geo.front.expanded (3.0f), 4.0f), juce::PathStrokeType (1.0f));

    // Cantoneiras
    const float s = bh * 0.125f;
    const bool silver = model == Clean;
    const juce::Point<float> corners[] = { b.getTopLeft(), b.getTopRight(), b.getBottomLeft(), b.getBottomRight() };
    for (int i = 0; i < 4; ++i)
    {
        const float sx = (i % 2 == 0) ? 1.0f : -1.0f, sy = (i < 2) ? 1.0f : -1.0f;
        auto cr = juce::Rectangle<float> (s, s).withCentre ({ corners[i].x + sx * s * 0.38f, corners[i].y + sy * s * 0.38f });
        juce::Path cp;
        cp.addRoundedRectangle (cr.getX(), cr.getY(), cr.getWidth(), cr.getHeight(), s * 0.22f, s * 0.22f,
                                i == 0, i == 1, i == 2, i == 3);
        dropShadow (g, cp, 6, { 0, 2 }, 0.8f);
        g.setGradientFill (silver ? steel (cr, juce::Colour (0xff8a8d93), juce::Colour (0xff222427))
                                  : steel (cr, juce::Colour (0xff4a4a4e), juce::Colour (0xff060606)));
        g.fillPath (cp);
        g.setColour (juce::Colours::white.withAlpha (silver ? 0.35f : 0.12f));
        g.strokePath (cp, juce::PathStrokeType (0.8f));
        // reflexo suave na cantoneira
        juce::ColourGradient spec (juce::Colours::white.withAlpha (silver ? 0.35f : 0.12f), cr.getX(), cr.getY(),
                                   juce::Colours::transparentWhite, cr.getCentreX(), cr.getCentreY(), false);
        g.setGradientFill (spec);
        g.fillPath (cp);
        screw (g, { cr.getCentreX() - sx * s * 0.18f, cr.getCentreY() - sy * s * 0.18f }, s * 0.07f,
               model == Brit ? juce::Colour (0xffb39657) : juce::Colour (0xff8a8c90));
    }
}

//==============================================================================
// Nomes, escalas e títulos de seção dos controles
void AmpHead::drawControlsArt (juce::Graphics& g, const Geometry& geo, juce::Colour ink, juce::Colour titleInk, juce::Colour lineInk)
{
    const auto& list = controls();
    const float R = geo.knobD * 0.5f;
    const auto lblFont = Fonts::condensedMedium (juce::jlimit (10.0f, 14.0f, geo.panel.getHeight() * 0.105f));
    const auto titleFont = Fonts::condensedBold (juce::jlimit (11.0f, 15.0f, geo.panel.getHeight() * 0.11f));
    const auto numFont = Fonts::condensedMedium (juce::jmax (7.5f, R * 0.30f));

    for (size_t i = 0; i < list.size(); ++i)
    {
        const auto c = geo.pos[i];
        drawTxt (g, list[i].label, { c.x, geo.labelY }, lblFont, ink);
        if (list[i].isSwitch) continue;
        for (int t = 0; t <= 10; ++t)
        {
            const float a = knobStart + (knobEnd - knobStart) * (float) t / 10.0f;
            const float r0 = R * 1.12f, r1 = R * ((t % 5 == 0) ? 1.26f : 1.2f);
            g.setColour (ink.withAlpha ((t % 5 == 0) ? 0.85f : 0.45f));
            g.drawLine (c.x + std::sin (a) * r0, c.y - std::cos (a) * r0, c.x + std::sin (a) * r1, c.y - std::cos (a) * r1, 0.9f);
        }
        g.setFont (numFont);
        g.setColour (ink.withAlpha (0.7f));
        for (int t : { 0, 5, 10 })
        {
            const float a = knobStart + (knobEnd - knobStart) * (float) t / 10.0f;
            const float rr = R * 1.48f;
            g.drawText (juce::String (t), juce::Rectangle<float> (16.0f, 10.0f).withCentre ({ c.x + std::sin (a) * rr, c.y - std::cos (a) * rr }),
                        juce::Justification::centred);
        }
    }

    const auto& titles = sectionTitles[model];
    for (int s = 0; s < (int) titles.size(); ++s)
    {
        float lo = 1e9f, hi = -1e9f;
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i].group == s) { lo = juce::jmin (lo, geo.pos[i].x); hi = juce::jmax (hi, geo.pos[i].x); }
        if (lo > hi) continue;
        const float pad = geo.knobD * 0.85f;
        sectionTitle (g, titles[(size_t) s], lo - pad, hi + pad, geo.titleY, titleFont, titleInk, lineInk);
    }
}

// Ponto entre duas seções (meio do caminho entre o último knob de uma e o primeiro da outra)
static float sectionBoundary (const AmpHead::Geometry& geo, const std::vector<AmpHead::Control>& list, int group)
{
    for (size_t i = 1; i < list.size(); ++i)
        if (list[i].group == group && list[i - 1].group != group)
            return (geo.pos[i - 1].x + geo.pos[i].x) * 0.5f;
    return 0.0f;
}

//==============================================================================
// AMP 01 — CLEAN 120
void AmpHead::drawClean (juce::Graphics& g, const Geometry& geo)
{
    const auto& list = controls();
    const auto p = geo.panel;
    const float bh = geo.body.getHeight();

    g.setColour (juce::Colour (0xff0b0b0d));
    g.fillRect (p);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (p.toNearestInt());
        brushed (g, p, 2, juce::Colours::white, 0.03f);
    }

    auto boxFor = [&] (float x0, float x1)
    {
        auto r = juce::Rectangle<float> (x0, p.getY() + 4.0f, x1 - x0, p.getHeight() - 8.0f);
        g.setColour (juce::Colour (0xff111113));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colour (0xff3a3a40));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRect (r.getX() + 3.0f, r.getY() + 1.0f, r.getWidth() - 6.0f, 1.0f);
    };
    const float b1 = sectionBoundary (geo, list, 1), b2 = sectionBoundary (geo, list, 2);
    const float pad = geo.knobD * 1.05f;
    boxFor (p.getX() + 4.0f, b1 - 3.0f);
    boxFor (b1 + 3.0f, b2 - 3.0f);
    const float chorusEnd = geo.pos.back().x + pad;
    boxFor (b2 + 3.0f, chorusEnd);

    // Power e placa do nome
    const float powerX0 = chorusEnd + 6.0f;
    const float plateX0 = p.getX() + p.getWidth() * 0.845f;
    boxFor (powerX0, plateX0 - 4.0f);
    const float pcx = (powerX0 + plateX0 - 4.0f) * 0.5f;
    const auto titleFont = Fonts::condensedBold (juce::jlimit (11.0f, 15.0f, p.getHeight() * 0.11f));
    drawTxt (g, "POWER", { pcx, geo.titleY }, titleFont, juce::Colour (0xffd9dadc));
    rocker (g, juce::Rectangle<float> (geo.knobD * 0.55f, geo.knobD * 0.8f).withCentre ({ pcx, geo.pos[0].y }), juce::Colour (0xff3a0d0b), true);
    drawTxt (g, "POWER", { pcx, geo.labelY }, Fonts::condensedMedium (juce::jlimit (10.0f, 14.0f, p.getHeight() * 0.105f)), juce::Colour (0xffcfd0d3));

    auto plate = juce::Rectangle<float> (plateX0, p.getY() + 4.0f, p.getRight() - plateX0 - 4.0f, p.getHeight() - 8.0f);
    g.setColour (juce::Colour (0xff060607));
    g.fillRoundedRectangle (plate, 3.0f);
    g.setColour (juce::Colour (0xff2c2c31));
    g.drawRoundedRectangle (plate, 3.0f, 1.0f);
    drawTxt (g, "CLEAN 120", { plate.getCentreX(), plate.getCentreY() - plate.getHeight() * 0.08f },
          Fonts::condensedMedium (plate.getHeight() * 0.25f), juce::Colour (0xffe8e9eb), plate.getWidth());
    drawTxt (g, "STEREO", { plate.getCentreX(), plate.getCentreY() + plate.getHeight() * 0.16f },
          Fonts::condensed (plate.getHeight() * 0.11f), juce::Colour (0xff8d8f94), plate.getWidth());

    drawControlsArt (g, geo, juce::Colour (0xffcfd0d3), juce::Colour (0xffe6e7e9), juce::Colour (0xff6c6d73));

    // Grade com moldura prateada
    const auto gr = geo.grille;
    g.setGradientFill (steel (gr, juce::Colour (0xffa9acb1), juce::Colour (0xff3e4045)));
    g.drawRect (gr, 1.8f);
    auto cloth = gr.reduced (3.0f);
    g.setColour (juce::Colour (0xff0d0d0f));
    g.fillRect (cloth);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (cloth.toNearestInt());
        const float step = juce::jmax (6.0f, bh * 0.027f);
        for (float x = cloth.getX(); x < cloth.getRight(); x += step)
        {
            g.setColour (juce::Colour (0xff6a6b72).withAlpha (0.6f));
            g.fillRect (x, cloth.getY(), 1.1f, cloth.getHeight());
        }
        for (float y = cloth.getY(); y < cloth.getBottom(); y += step)
        {
            g.setColour (juce::Colour (0xff5b5c62).withAlpha (0.45f));
            g.fillRect (cloth.getX(), y, cloth.getWidth(), 1.1f);
        }
        juce::Random rng (5);
        for (int i = 0; i < (int) (cloth.getWidth() * cloth.getHeight() * 0.05f); ++i)
        {
            g.setColour (juce::Colours::white.withAlpha (0.03f * rng.nextFloat()));
            g.fillRect (cloth.getX() + rng.nextFloat() * cloth.getWidth(), cloth.getY() + rng.nextFloat() * cloth.getHeight(), 1.0f, 1.0f);
        }
        juce::ColourGradient vg (juce::Colours::transparentBlack, cloth.getCentreX(), cloth.getY() + cloth.getHeight() * 0.3f,
                                 juce::Colours::black.withAlpha (0.55f), cloth.getX(), cloth.getBottom(), true);
        g.setGradientFill (vg);
        g.fillRect (cloth);
    }

    // Emblema no canto superior esquerdo da grade
    auto badge = juce::Rectangle<float> (cloth.getX() + cloth.getWidth() * 0.018f, cloth.getY() + cloth.getHeight() * 0.07f,
                                         cloth.getWidth() * 0.27f, cloth.getHeight() * 0.26f);
    g.setColour (juce::Colour (0xe0141416));
    g.fillRect (badge);
    auto nameArea = badge.reduced (badge.getWidth() * 0.04f, 0).withTrimmedBottom (badge.getHeight() * 0.34f).withTrimmedTop (badge.getHeight() * 0.06f);
    {
        auto bf = Fonts::badge (nameArea.getHeight() * 0.86f);
        bf.setHorizontalScale (1.12f);
        g.setFont (bf);
        g.setColour (juce::Colours::black);
        g.drawFittedText ("CLEAN 120", nameArea.translated (1.0f, 1.5f).toNearestInt(), juce::Justification::centred, 1, 1.0f);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff2f3f5), 0, nameArea.getY(), juce::Colour (0xff9a9da3), 0, nameArea.getBottom(), false));
    g.drawFittedText ("CLEAN 120", nameArea.toNearestInt(), juce::Justification::centred, 1, 1.0f);
    drawTxt (g, "PURE TONE.  NO COMPROMISE", { badge.getCentreX(), badge.getY() + badge.getHeight() * 0.78f },
          Fonts::wide (juce::jmax (8.0f, badge.getHeight() * 0.13f)), juce::Colour (0xffa9acb2), badge.getWidth());
}

//==============================================================================
// AMP 02 — BRIT410
void AmpHead::drawBrit (juce::Graphics& g, const Geometry& geo)
{
    const auto f = geo.grille;
    const auto p = geo.panel;

    // Fáscia preta superior com friso dourado (degrau na direita)
    g.setColour (juce::Colour (0xff050505));
    g.fillRect (f);
    const float stepY = f.getBottom() - f.getHeight() * 0.20f;
    const float diagTop = f.getX() + f.getWidth() * 0.775f;
    const float diagBot = f.getX() + f.getWidth() * 0.735f;
    juce::Path fascia;
    fascia.startNewSubPath (f.getX() + 3.0f, f.getY() + 3.0f);
    fascia.lineTo (f.getRight() - 3.0f, f.getY() + 3.0f);
    fascia.lineTo (f.getRight() - 3.0f, stepY);
    fascia.lineTo (diagTop, stepY);
    fascia.lineTo (diagBot, f.getBottom() - 2.0f);
    fascia.lineTo (f.getX() + 3.0f, f.getBottom() - 2.0f);
    fascia.closeSubPath();
    juce::ColourGradient fg (juce::Colour (0xff141312), 0, f.getY(), juce::Colour (0xff070707), 0, f.getBottom(), false);
    g.setGradientFill (fg);
    g.fillPath (fascia);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (fascia);
        leather (g, f, 33, 0.8f);
    }
    juce::ColourGradient gold (juce::Colour (0xffe8cf91), f.getX(), f.getY(), juce::Colour (0xff8c6c33), f.getRight(), f.getBottom(), false);
    gold.addColour (0.5, juce::Colour (0xffc9a95f));
    g.setGradientFill (gold);
    g.strokePath (fascia, juce::PathStrokeType (1.8f));

    // Letreiro BRIT410
    {
        auto area = juce::Rectangle<float> (f.getX(), f.getY() + f.getHeight() * 0.33f, f.getWidth(), f.getHeight() * 0.26f);
        auto font = Fonts::badge (area.getHeight() * 0.95f);
        font.setHorizontalScale (1.18f);
        g.setFont (font);
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.drawText ("BRIT410", area.translated (2.0f, 3.5f), juce::Justification::centred);
        g.setColour (juce::Colour (0xff3a2b12));
        g.drawText ("BRIT410", area.translated (0.0f, 1.5f), juce::Justification::centred);
        juce::ColourGradient lg (juce::Colour (0xfff7e6bd), 0, area.getY() + area.getHeight() * 0.1f,
                                 juce::Colour (0xff8a6a32), 0, area.getBottom(), false);
        lg.addColour (0.48, juce::Colour (0xffd6bb7c));
        lg.addColour (0.55, juce::Colour (0xffa9884b));
        g.setGradientFill (lg);
        g.drawText ("BRIT410", area, juce::Justification::centred);
    }

    // Painel dourado escovado
    juce::ColourGradient pg (juce::Colour (0xffcdb27a), 0, p.getY(), juce::Colour (0xff8a7142), 0, p.getBottom(), false);
    pg.addColour (0.3, juce::Colour (0xffbfa36c));
    g.setGradientFill (pg);
    g.fillRect (p);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (p.toNearestInt());
        brushed (g, p, 7, juce::Colour (0xfffff4d8), 0.35f);
        brushed (g, p, 8, juce::Colours::black, 0.10f);
        juce::ColourGradient vg (juce::Colours::transparentBlack, p.getCentreX(), p.getCentreY(),
                                 juce::Colour (0xff1e1408).withAlpha (0.65f), p.getX(), p.getCentreY(), true);
        g.setGradientFill (vg);
        g.fillRect (p);
        juce::ColourGradient warm (juce::Colour (0x40fff0c8), p.getCentreX(), p.getY(), juce::Colour (0x00fff0c8), p.getCentreX(), p.getBottom(), false);
        g.setGradientFill (warm);
        g.fillRect (p);
    }
    g.setColour (juce::Colour (0xff2a1d0b).withAlpha (0.8f));
    g.drawRect (p, 1.5f);
    g.setColour (juce::Colour (0xfff2dfae).withAlpha (0.5f));
    g.fillRect (p.getX(), p.getY() + 1.5f, p.getWidth(), 1.0f);

    const auto ink = juce::Colour (0xff2a1f10);
    const auto lblFont = Fonts::condensedMedium (juce::jlimit (10.0f, 14.0f, p.getHeight() * 0.105f));
    const float ky = geo.pos[0].y;

    // Power (vermelho) e Standby (preto)
    const float rw = geo.knobD * 0.62f, rh = geo.knobD * 0.85f;
    const juce::Point<float> pwr (p.getX() + p.getWidth() * 0.045f, ky), sby (p.getX() + p.getWidth() * 0.105f, ky);
    rocker (g, juce::Rectangle<float> (rw, rh).withCentre (pwr), juce::Colour (0xffb3150f), false);
    rocker (g, juce::Rectangle<float> (rw, rh).withCentre (sby), juce::Colour (0xff1a1a1a), false);
    drawTxt (g, "POWER", { pwr.x, geo.labelY }, lblFont, ink);
    drawTxt (g, "STANDBY", { sby.x, geo.labelY }, lblFont, ink);

    const float divX = p.getX() + p.getWidth() * 0.155f;
    g.setColour (ink.withAlpha (0.35f));
    g.fillRect (divX, p.getY() + p.getHeight() * 0.12f, 1.0f, p.getHeight() * 0.76f);
    const juce::Point<float> in (p.getX() + p.getWidth() * 0.955f, ky);
    jack (g, in, geo.knobD * 0.2f);
    drawTxt (g, "INPUT", { in.x, geo.labelY }, lblFont, ink);

    // Pequenos LEDs vermelhos de status no início de cada seção
    const auto& list = controls();
    for (size_t i = 0; i < list.size(); ++i)
        if (i == 0 || list[i].group != list[i - 1].group)
            led (g, { geo.pos[i].x - geo.knobD * 0.75f, geo.titleY + geo.knobD * 0.32f }, 2.2f, juce::Colour (0xffff2a1a));

    drawControlsArt (g, geo, ink, ink, ink.withAlpha (0.55f));
}

//==============================================================================
// AMP 03 — 51 BLOCK
void AmpHead::drawBlock (juce::Graphics& g, const Geometry& geo)
{
    const auto gr = geo.grille;
    const float bh = geo.body.getHeight();

    g.setColour (juce::Colour (0xff070707));
    g.fillRect (gr);

    // Malha colmeia: chapa de metal com furos hexagonais
    const float hr = juce::jmax (4.5f, bh * 0.024f);
    const float hw = std::sqrt (3.0f) * hr;
    juce::Path holes;
    int row = 0;
    for (float y = gr.getY(); y < gr.getBottom() + hr; y += hr * 1.5f, ++row)
        for (float x = gr.getX() + ((row % 2) ? hw * 0.5f : 0.0f); x < gr.getRight() + hw; x += hw)
        {
            juce::Path hex;
            for (int k = 0; k < 6; ++k)
            {
                const float a = juce::MathConstants<float>::pi / 3.0f * (float) k + juce::MathConstants<float>::pi / 6.0f;
                const float rr = hr * 0.78f;
                if (k == 0) hex.startNewSubPath (x + std::cos (a) * rr, y + std::sin (a) * rr);
                else hex.lineTo (x + std::cos (a) * rr, y + std::sin (a) * rr);
            }
            hex.closeSubPath();
            holes.addPath (hex);
        }

    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (gr.toNearestInt());
        g.setGradientFill (steel (gr, juce::Colour (0xff2c2d31), juce::Colour (0xff0c0c0d)));
        g.fillRect (gr);
        {
            juce::Graphics::ScopedSaveState s2 (g);
            g.reduceClipRegion (holes);
            g.setColour (juce::Colour (0xff030303));
            g.fillRect (gr);
            for (float fx : { 0.25f, 0.75f })
            {
                const juce::Point<float> c (gr.getX() + gr.getWidth() * fx, gr.getY() + gr.getHeight() * 0.55f);
                const float rr = gr.getHeight() * 0.42f;
                juce::ColourGradient glow (juce::Colour (0xccff6a1c), c.x, c.y, juce::Colour (0x00ff4a10), c.x + rr, c.y, true);
                glow.addColour (0.35, juce::Colour (0x66c2410e));
                g.setGradientFill (glow);
                g.fillEllipse (c.x - rr, c.y - rr, rr * 2, rr * 2);
                for (float dx : { -0.35f, 0.0f, 0.35f })
                {
                    auto tube = juce::Rectangle<float> (rr * 0.22f, rr * 0.9f).withCentre ({ c.x + dx * rr, c.y + rr * 0.05f });
                    g.setColour (juce::Colours::black.withAlpha (0.45f));
                    g.fillRoundedRectangle (tube, tube.getWidth() * 0.45f);
                    g.setColour (juce::Colour (0xffff7a2a).withAlpha (0.5f));
                    g.fillEllipse (tube.getCentreX() - 2.0f, tube.getCentreY(), 4.0f, 4.0f);
                }
            }
        }
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.strokePath (holes, juce::PathStrokeType (0.7f), juce::AffineTransform::translation (0, 0.7f));
        juce::ColourGradient vg (juce::Colours::transparentBlack, gr.getCentreX(), gr.getCentreY(),
                                 juce::Colours::black.withAlpha (0.55f), gr.getX(), gr.getY(), true);
        g.setGradientFill (vg);
        g.fillRect (gr);
    }

    // Estrutura angular de metal
    const float t = juce::jmax (5.0f, bh * 0.022f);
    auto bar = [&] (juce::Point<float> a, juce::Point<float> b2)
    {
        juce::Path pth; pth.startNewSubPath (a); pth.lineTo (b2);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.strokePath (pth, juce::PathStrokeType (t + 2.0f), juce::AffineTransform::translation (0, 2.0f));
        g.setColour (juce::Colour (0xff1e1f22));
        g.strokePath (pth, juce::PathStrokeType (t));
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        g.strokePath (pth, juce::PathStrokeType (1.0f), juce::AffineTransform::translation (0, -t * 0.4f));
    };
    const auto in = gr.reduced (t * 0.5f);
    bar (in.getTopLeft(), in.getTopRight());
    bar (in.getBottomLeft(), in.getBottomRight());
    bar (in.getTopLeft(), in.getBottomLeft());
    bar (in.getTopRight(), in.getBottomRight());
    const juce::Point<float> lTop (in.getX() + in.getWidth() * 0.025f, in.getY()), lBot (in.getX() + in.getWidth() * 0.115f, in.getBottom());
    const juce::Point<float> rTop (in.getRight() - in.getWidth() * 0.025f, in.getY()), rBot (in.getRight() - in.getWidth() * 0.115f, in.getBottom());
    bar (lTop, lBot);
    bar (rTop, rBot);

    // Emblema central "51 BLOCK"
    auto badge = juce::Rectangle<float> (gr.getWidth() * 0.27f, gr.getHeight() * 0.36f).withCentre (gr.getCentre().translated (0, -gr.getHeight() * 0.03f));
    bar ({ gr.getCentreX(), in.getY() }, { gr.getCentreX(), badge.getY() });
    bar ({ gr.getCentreX(), badge.getBottom() }, { gr.getCentreX(), in.getBottom() });
    for (auto pt : { in.getTopLeft(), in.getTopRight(), in.getBottomLeft(), in.getBottomRight(), lBot, rBot,
                     juce::Point<float> (gr.getCentreX(), in.getY() + t * 1.6f), juce::Point<float> (gr.getCentreX(), in.getBottom() - t * 1.6f) })
        screw (g, pt, t * 0.38f, juce::Colour (0xff6d6f74));

    const auto bp = rounded (badge, badge.getHeight() * 0.12f);
    dropShadow (g, bp, 14, { 0, 6 }, 0.9f);
    g.setGradientFill (steel (badge, juce::Colour (0xffe4e6e9), juce::Colour (0xff4c4f55)));
    g.fillPath (bp);
    auto inner = badge.reduced (badge.getHeight() * 0.09f);
    const auto ip = rounded (inner, inner.getHeight() * 0.08f);
    juce::ColourGradient ig (juce::Colour (0xff2a2b2f), 0, inner.getY(), juce::Colour (0xff0b0b0c), 0, inner.getBottom(), false);
    g.setGradientFill (ig);
    g.fillPath (ip);
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.strokePath (ip, juce::PathStrokeType (0.8f));
    {
        g.setFont (Fonts::badge (inner.getHeight() * 0.66f));
        g.setColour (juce::Colours::black);
        g.drawText ("51 BLOCK", inner.translated (1.5f, 2.0f), juce::Justification::centred);
        juce::ColourGradient tg (juce::Colour (0xfff4f5f7), 0, inner.getY() + inner.getHeight() * 0.25f, juce::Colour (0xff7f838a), 0, inner.getBottom() - inner.getHeight() * 0.2f, false);
        tg.addColour (0.5, juce::Colour (0xffc3c6cb));
        g.setGradientFill (tg);
        g.drawText ("51 BLOCK", inner, juce::Justification::centred);
    }

    // Painel de controles preto
    const auto p = geo.panel;
    g.setColour (juce::Colour (0xff0a0a0b));
    g.fillRect (p);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (p.toNearestInt());
        brushed (g, p, 9, juce::Colours::white, 0.035f);
    }
    const auto& list = controls();
    const float split = sectionBoundary (geo, list, 1);
    auto box = [&] (float x0, float x1)
    {
        auto r = juce::Rectangle<float> (x0, p.getY() + 4.0f, x1 - x0, p.getHeight() - 8.0f);
        g.setColour (juce::Colour (0xff3d3d42));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRect (r.getX() + 3.0f, r.getY() + 1.0f, r.getWidth() - 6.0f, 1.0f);
    };
    const float powerX = p.getX() + p.getWidth() * 0.915f;
    box (p.getX() + 4.0f, split - 4.0f);
    box (split + 4.0f, p.getX() + p.getWidth() * 0.855f);
    box (p.getX() + p.getWidth() * 0.855f + 6.0f, p.getRight() - 4.0f);

    const auto ink = juce::Colour (0xffd2d3d6);
    const auto lblFont = Fonts::condensedMedium (juce::jlimit (10.0f, 14.0f, p.getHeight() * 0.105f));
    const auto titleFont = Fonts::condensedBold (juce::jlimit (11.0f, 15.0f, p.getHeight() * 0.11f));
    const juce::Point<float> jc (p.getX() + p.getWidth() * 0.055f, geo.pos[0].y);
    jack (g, jc, geo.knobD * 0.24f);
    drawTxt (g, "INPUT", { jc.x, geo.labelY }, lblFont, ink);
    g.setColour (ink.withAlpha (0.25f));
    g.fillRect (p.getX() + p.getWidth() * 0.1f, p.getY() + p.getHeight() * 0.3f, 1.0f, p.getHeight() * 0.45f);

    drawTxt (g, "POWER", { powerX, geo.titleY }, titleFont, ink);
    led (g, { powerX, geo.titleY + geo.knobD * 0.42f }, 2.8f, juce::Colour (0xffff2a1a));
    rocker (g, juce::Rectangle<float> (geo.knobD * 0.6f, geo.knobD * 0.82f).withCentre ({ powerX, geo.pos[0].y + geo.knobD * 0.12f }), juce::Colour (0xff1b1b1c), true);
    drawTxt (g, "POWER", { powerX, geo.labelY }, lblFont, ink);

    drawControlsArt (g, geo, ink, ink, juce::Colour (0xff55565c));
}

//==============================================================================
// AMP 04 — REC
void AmpHead::drawRec (juce::Graphics& g, const Geometry& geo)
{
    const auto face = geo.grille;
    const float bh = geo.body.getHeight();

    // Chapa xadrez escura (diamond plate)
    g.setGradientFill (steel (face, juce::Colour (0xff2a2b2e), juce::Colour (0xff121213)));
    g.fillRect (face);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (face.toNearestInt());
        const float L = juce::jmax (12.0f, bh * 0.06f);
        const float sx = L * 0.95f, sy = L * 0.95f;
        int r = 0;
        for (float y = face.getY() - sy; y < face.getBottom() + sy; y += sy * 0.5f, ++r)
            for (float x = face.getX() - sx + ((r % 2) ? sx * 0.5f : 0.0f); x < face.getRight() + sx; x += sx)
            {
                const bool flip = (r % 2) == 0;
                juce::Path d;
                d.startNewSubPath (-L * 0.5f, 0.0f);
                d.quadraticTo (0.0f, -L * 0.09f, L * 0.5f, 0.0f);
                d.quadraticTo (0.0f, L * 0.09f, -L * 0.5f, 0.0f);
                d.closeSubPath();
                const auto tr = juce::AffineTransform::rotation (flip ? 0.785f : -0.785f).translated (x, y);
                g.setColour (juce::Colours::black.withAlpha (0.65f));
                g.fillPath (d, tr.translated (1.2f, 1.6f));
                juce::ColourGradient lg (juce::Colour (0xff8a8c91), x - L * 0.3f, y - L * 0.3f, juce::Colour (0xff1c1c1e), x + L * 0.3f, y + L * 0.3f, false);
                g.setGradientFill (lg);
                g.fillPath (d, tr);
            }
        juce::ColourGradient vg (juce::Colours::transparentBlack, face.getCentreX(), face.getY() + face.getHeight() * 0.3f,
                                 juce::Colours::black.withAlpha (0.6f), face.getX(), face.getBottom(), true);
        g.setGradientFill (vg);
        g.fillRect (face);
    }

    // Conjunto de ventilação central com o emblema
    const float vy0 = face.getY() + face.getHeight() * 0.16f, vy1 = face.getY() + face.getHeight() * 0.50f;
    auto vent = juce::Rectangle<float> (face.getX() + face.getWidth() * 0.19f, vy0, face.getWidth() * 0.62f, vy1 - vy0);
    auto tab = juce::Rectangle<float> (face.getWidth() * 0.27f, (vy1 - vy0) * 1.25f).withCentre (vent.getCentre());
    juce::Path frame;
    frame.addRoundedRectangle (vent, vent.getHeight() * 0.22f);
    frame.addRoundedRectangle (tab, tab.getHeight() * 0.14f);
    dropShadow (g, frame, 16, { 0, 6 }, 0.9f);
    g.setGradientFill (steel (tab, juce::Colour (0xff5c5f64), juce::Colour (0xff111113)));
    g.fillPath (frame);
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.strokePath (frame, juce::PathStrokeType (0.8f));

    // Aberturas verticais (com brilho quente das válvulas)
    auto slotsArea = vent.reduced (vent.getHeight() * 0.18f);
    const float slotW = juce::jmax (3.5f, bh * 0.016f), gap = slotW * 1.25f;
    for (float x = slotsArea.getX() + slotW; x < slotsArea.getRight() - slotW; x += slotW + gap)
    {
        if (x > tab.getX() - slotW * 1.5f && x < tab.getRight() + slotW * 0.5f) continue;
        auto slot = juce::Rectangle<float> (x, slotsArea.getY(), slotW, slotsArea.getHeight());
        g.setColour (juce::Colour (0xff020202));
        g.fillRoundedRectangle (slot, slotW * 0.5f);
        juce::ColourGradient warm (juce::Colour (0x00ff5a1a), 0, slot.getY(), juce::Colour (0x88ff5a1a), 0, slot.getCentreY(), false);
        warm.addColour (0.85, juce::Colour (0x00ff5a1a));
        g.setGradientFill (warm);
        g.fillRoundedRectangle (slot.reduced (slotW * 0.25f, 0), slotW * 0.3f);
        g.setColour (juce::Colours::white.withAlpha (0.2f));
        g.fillRect (slot.getRight() - 0.8f, slot.getY() + 2.0f, 0.8f, slot.getHeight() - 4.0f);
    }

    // Emblema REC
    auto badge = tab.reduced (tab.getHeight() * 0.1f, tab.getHeight() * 0.12f);
    const auto bp = rounded (badge, 3.0f);
    g.setColour (juce::Colour (0xff0a0a0b));
    g.fillPath (bp);
    g.setGradientFill (steel (badge, juce::Colour (0xffd5d7db), juce::Colour (0xff44464b)));
    g.strokePath (bp, juce::PathStrokeType (2.0f));
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.strokePath (rounded (badge.reduced (3.0f), 2.0f), juce::PathStrokeType (0.6f));
    for (auto pt : { badge.getTopLeft().translated (6, 6), badge.getTopRight().translated (-6, 6),
                     badge.getBottomLeft().translated (6, -6), badge.getBottomRight().translated (-6, -6) })
        screw (g, pt, 2.4f);
    {
        auto nameArea = badge.withTrimmedBottom (badge.getHeight() * 0.32f).withTrimmedTop (badge.getHeight() * 0.08f);
        juce::GlyphArrangement ga;
        ga.addFittedText (Fonts::serif (nameArea.getHeight() * 0.8f), "REC", nameArea.getX(), nameArea.getY(),
                          nameArea.getWidth(), nameArea.getHeight(), juce::Justification::centred, 1);
        juce::Path letters;
        ga.createPath (letters);
        const float sw = nameArea.getHeight() * 0.045f;
        g.setColour (juce::Colours::black);
        g.fillPath (letters, juce::AffineTransform::translation (1.5f, 2.0f));
        g.strokePath (letters, juce::PathStrokeType (sw), juce::AffineTransform::translation (1.5f, 2.0f));
        juce::ColourGradient tg (juce::Colour (0xfff2f3f5), 0, nameArea.getY() + nameArea.getHeight() * 0.2f, juce::Colour (0xff86898f), 0, nameArea.getBottom(), false);
        tg.addColour (0.5, juce::Colour (0xffc9ccd0));
        g.setGradientFill (tg);
        g.fillPath (letters);
        g.strokePath (letters, juce::PathStrokeType (sw));
        sectionTitle (g, "A M P L I F I C A T I O N", badge.getX() + badge.getWidth() * 0.12f, badge.getRight() - badge.getWidth() * 0.12f,
                      badge.getBottom() - badge.getHeight() * 0.2f, Fonts::wide (badge.getHeight() * 0.075f),
                      juce::Colour (0xff9fa2a8), juce::Colour (0xff55585e));
    }

    // Faixa de controles
    const auto p = geo.panel.reduced (4.0f, 0.0f).withTrimmedBottom (4.0f);
    g.setColour (juce::Colour (0xf20a0a0b));
    g.fillRect (p);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.fillRect (p.getX(), p.getY(), p.getWidth(), 1.0f);
    g.setColour (juce::Colours::black);
    g.fillRect (p.getX(), p.getY() - 2.0f, p.getWidth(), 2.0f);

    const auto ink = juce::Colour (0xffcfd0d3);
    const auto lblFont = Fonts::condensedMedium (juce::jlimit (10.0f, 14.0f, p.getHeight() * 0.105f));
    const auto& list = controls();
    for (int s = 1; s < 3; ++s)
    {
        const float x = sectionBoundary (geo, list, s);
        g.setColour (juce::Colour (0xff3a3a3f));
        g.fillRect (x, p.getY() + p.getHeight() * 0.12f, 1.0f, p.getHeight() * 0.78f);
    }
    const juce::Point<float> jc (p.getX() + p.getWidth() * 0.05f, geo.pos[0].y);
    jack (g, jc, geo.knobD * 0.24f);
    drawTxt (g, "INPUT", { jc.x, geo.labelY }, lblFont, ink);

    const juce::Point<float> sb (p.getX() + p.getWidth() * 0.885f, geo.pos[0].y + geo.knobD * 0.2f);
    const juce::Point<float> pw (p.getX() + p.getWidth() * 0.945f, geo.pos[0].y + geo.knobD * 0.2f);
    toggleLever (g, sb, geo.knobD * 0.17f);
    toggleLever (g, pw, geo.knobD * 0.17f);
    led (g, { sb.x, geo.titleY + 4.0f }, 2.4f, juce::Colour (0xffff2a1a), false);
    led (g, { pw.x, geo.titleY + 4.0f }, 2.8f, juce::Colour (0xffff2a1a), true);
    const auto smallFont = Fonts::condensed (juce::jlimit (8.0f, 11.0f, p.getHeight() * 0.07f));
    drawTxt (g, "STANDBY", { sb.x, geo.labelY }, smallFont, ink);
    drawTxt (g, "POWER", { pw.x, geo.labelY }, smallFont, ink);

    drawControlsArt (g, geo, ink, juce::Colour (0xffe2e3e5), juce::Colour (0xff4a4b50));
}

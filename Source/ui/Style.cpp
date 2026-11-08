#include "Style.h"
#include <BinaryData.h>

namespace ui
{
static juce::Typeface::Ptr loadFace (const char* data, int size)
{
    return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
}

static juce::Typeface::Ptr faceMedium()   { static auto t = loadFace (BinaryData::BarlowCondensedMedium_ttf, BinaryData::BarlowCondensedMedium_ttfSize); return t; }
static juce::Typeface::Ptr faceSemi()     { static auto t = loadFace (BinaryData::BarlowCondensedSemiBold_ttf, BinaryData::BarlowCondensedSemiBold_ttfSize); return t; }
static juce::Typeface::Ptr faceBold()     { static auto t = loadFace (BinaryData::BarlowCondensedBold_ttf, BinaryData::BarlowCondensedBold_ttfSize); return t; }
static juce::Typeface::Ptr faceLogo()     { static auto t = loadFace (BinaryData::BarlowCondensedBlackItalic_ttf, BinaryData::BarlowCondensedBlackItalic_ttfSize); return t; }
static juce::Typeface::Ptr faceBadge()    { static auto t = loadFace (BinaryData::RussoOneRegular_ttf, BinaryData::RussoOneRegular_ttfSize); return t; }

static juce::Typeface::Ptr faceSig()      { static auto t = loadFace (BinaryData::MrsSaintDelafieldRegular_ttf, BinaryData::MrsSaintDelafieldRegular_ttfSize); return t; }
static juce::Typeface::Ptr faceWide()     { static auto t = loadFace (BinaryData::MichromaRegular_ttf, BinaryData::MichromaRegular_ttfSize); return t; }

static juce::Typeface::Ptr faceSerif()    { static auto t = loadFace (BinaryData::Cinzel_ttf, BinaryData::Cinzel_ttfSize); return t; }

static juce::Font make (juce::Typeface::Ptr t, float h) { return juce::Font (juce::FontOptions (t).withHeight (h)); }

juce::Font Fonts::condensed (float h)       { return make (faceSemi(), h); }
juce::Font Fonts::condensedBold (float h)   { return make (faceBold(), h); }
juce::Font Fonts::condensedMedium (float h) { return make (faceMedium(), h); }
juce::Font Fonts::logo (float h)            { return make (faceLogo(), h); }
juce::Font Fonts::badge (float h)           { return make (faceBadge(), h); }
juce::Font Fonts::signature (float h)       { return make (faceSig(), h); }
juce::Font Fonts::wide (float h)            { return make (faceWide(), h); }
juce::Font Fonts::serif (float h)           { return make (faceSerif(), h); }

void drawLogo (juce::Graphics& g, juce::Rectangle<float> area)
{
    const float h = area.getHeight();
    const float boltW = h * 0.62f;
    const float boltH = h * 1.0f;
    const float x0 = area.getX(), y0 = area.getY();

    auto font = Fonts::logo (h * 1.05f);
    const juce::String word ("VALVED");
    juce::GlyphArrangement ga;
    ga.addLineOfText (font, word, 0.0f, 0.0f);
    const auto tb = ga.getBoundingBox (0, -1, true);

    const float textX = x0 + boltW * 1.02f;
    const float baseline = y0 + h * 0.80f;

    // Raio: desce na lateral esquerda e sua ponta inferior vira o sublinhado
    const float bx = x0, by = y0 - h * 0.02f;
    juce::Path bolt;
    bolt.startNewSubPath (bx + boltW * 0.70f, by);
    bolt.lineTo (bx + boltW * 0.08f, by + boltH * 0.56f);
    bolt.lineTo (bx + boltW * 0.44f, by + boltH * 0.56f);
    bolt.lineTo (bx + boltW * 0.16f, by + boltH * 1.02f);
    // sublinhado afinando até o fim da palavra
    const float ulY = baseline + h * 0.10f;
    const float ulEnd = textX + tb.getWidth() + h * 0.10f;
    bolt.lineTo (ulEnd, ulY - h * 0.015f);
    bolt.lineTo (ulEnd - h * 0.05f, ulY - h * 0.04f);
    bolt.lineTo (bx + boltW * 0.60f, ulY - h * 0.095f);
    bolt.lineTo (bx + boltW * 0.98f, by + boltH * 0.38f);
    bolt.lineTo (bx + boltW * 0.60f, by + boltH * 0.38f);
    bolt.lineTo (bx + boltW * 1.00f, by);
    bolt.closeSubPath();

    juce::ColourGradient grad (juce::Colour (0xffffd166), bx, by,
                               accent2, ulEnd, ulY, false);
    grad.addColour (0.35, accent);
    g.setGradientFill (grad);
    g.fillPath (bolt);

    // Texto
    g.setColour (juce::Colours::white);
    g.setFont (font);
    g.drawSingleLineText (word, juce::roundToInt (textX), juce::roundToInt (baseline));
}
} // namespace ui

using namespace ui;

//==============================================================================
ValvedLook::ValvedLook()
{
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, text);
    setColour (juce::ComboBox::backgroundColourId, panelHi);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, line);
    setColour (juce::ComboBox::arrowColourId, accent);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.35f));
    setColour (juce::TextButton::buttonColourId, panelHi);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, textDim);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::TooltipWindow::backgroundColourId, panelHi);
}

juce::Typeface::Ptr ValvedLook::getTypefaceForFont (const juce::Font& f)
{
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return Fonts::condensed (f.getHeight()).getTypefacePtr();
    return LookAndFeel_V4::getTypefaceForFont (f);
}

juce::Font ValvedLook::getLabelFont (juce::Label& l)   { return Fonts::condensed (juce::jmax (12.0f, l.getFont().getHeight())); }
juce::Font ValvedLook::getComboBoxFont (juce::ComboBox&) { return Fonts::condensed (17.0f); }
juce::Font ValvedLook::getPopupMenuFont()               { return Fonts::condensed (17.0f); }

void ValvedLook::drawModernKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float pos, float a0, float a1)
{
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f - 2.0f;
    const auto c = bounds.getCentre();
    const float angle = a0 + pos * (a1 - a0);
    const float arcR = radius - 2.0f;

    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, a0, a1, true);
    g.setColour (line);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path value;
    value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, a0, angle, true);
    g.setColour (accent);
    g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float bodyR = arcR - 6.0f;
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (c.x - bodyR, c.y - bodyR + 2.0f, bodyR * 2, bodyR * 2);
    juce::ColourGradient grad (juce::Colour (0xff34343b), c.x, c.y - bodyR,
                               juce::Colour (0xff141417), c.x, c.y + bodyR, false);
    g.setGradientFill (grad);
    g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2, bodyR * 2);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawEllipse (c.x - bodyR + 0.5f, c.y - bodyR + 0.5f, bodyR * 2 - 1, bodyR * 2 - 1, 1.0f);

    juce::Path p;
    p.addRoundedRectangle (-1.25f, -bodyR + 3.0f, 2.5f, bodyR * 0.42f, 1.2f);
    g.setColour (text);
    g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c));
}

static void knurl (juce::Graphics& g, juce::Point<float> c, float r, int n, juce::Colour light, juce::Colour dark)
{
    for (int i = 0; i < n; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / (float) n;
        const juce::Point<float> p1 (c.x + std::sin (a) * r * 0.86f, c.y - std::cos (a) * r * 0.86f);
        const juce::Point<float> p2 (c.x + std::sin (a) * r * 0.99f, c.y - std::cos (a) * r * 0.99f);
        const float lit = 0.5f + 0.5f * std::cos (a + 0.8f);
        g.setColour (dark.interpolatedWith (light, lit));
        g.drawLine ({ p1, p2 }, 1.2f);
    }
}

void ValvedLook::drawAmpKnob (juce::Graphics& g, juce::Rectangle<float> r, float angle, int style)
{
    const auto c = r.getCentre();
    const float R = juce::jmin (r.getWidth(), r.getHeight()) / 2.0f - 1.0f;

    // Sombra
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (c.x - R + 2.0f, c.y - R + 4.0f, R * 2, R * 2);

    auto disc = [&g, c] (float rad, juce::ColourGradient grad)
    {
        g.setGradientFill (grad);
        g.fillEllipse (c.x - rad, c.y - rad, rad * 2, rad * 2);
    };
    auto rot = juce::AffineTransform::rotation (angle).translated (c);

    switch (style)
    {
        case KnobSilver: // saia preta + tampa de alumínio usinado
        {
            disc (R, juce::ColourGradient (juce::Colour (0xff3a3a3e), c.x, c.y - R, juce::Colour (0xff0d0d0f), c.x, c.y + R, false));
            knurl (g, c, R, 36, juce::Colour (0xff5a5a60), juce::Colour (0xff101012));
            const float cr = R * 0.66f;
            juce::ColourGradient al (juce::Colour (0xfff2f2f2), c.x - cr * 0.6f, c.y - cr * 0.8f,
                                     juce::Colour (0xff8d9094), c.x + cr * 0.7f, c.y + cr, true);
            al.addColour (0.5, juce::Colour (0xffc9cbce));
            disc (cr, al);
            g.setColour (juce::Colours::black.withAlpha (0.08f));
            for (float k = 0.2f; k < 1.0f; k += 0.12f)
                g.drawEllipse (c.x - cr * k, c.y - cr * k, cr * 2 * k, cr * 2 * k, 0.8f);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.drawEllipse (c.x - cr, c.y - cr, cr * 2, cr * 2, 1.0f);
            juce::Path p; p.addRoundedRectangle (-1.3f, -R + 1.0f, 2.6f, R * 0.42f, 1.0f);
            g.setColour (juce::Colours::white); g.fillPath (p, rot);
            juce::Path p2; p2.addRoundedRectangle (-1.0f, -cr + 2.0f, 2.0f, cr * 0.55f, 1.0f);
            g.setColour (juce::Colour (0xff222222)); g.fillPath (p2, rot);
            break;
        }
        case KnobGoldCap: // corpo preto serrilhado + tampa dourada
        {
            disc (R, juce::ColourGradient (juce::Colour (0xff2e2b28), c.x, c.y - R, juce::Colour (0xff070707), c.x, c.y + R, false));
            knurl (g, c, R, 28, juce::Colour (0xff4c4844), juce::Colour (0xff050505));
            const float cr = R * 0.52f;
            juce::ColourGradient gold (juce::Colour (0xfffff0b5), c.x - cr * 0.5f, c.y - cr * 0.8f,
                                       juce::Colour (0xff8f6b22), c.x + cr * 0.6f, c.y + cr, true);
            gold.addColour (0.45, juce::Colour (0xffd9b25c));
            disc (cr, gold);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.drawEllipse (c.x - cr, c.y - cr, cr * 2, cr * 2, 1.0f);
            juce::Path p; p.addRoundedRectangle (-1.4f, -R + 2.0f, 2.8f, R - cr - 1.0f, 1.0f);
            g.setColour (juce::Colour (0xfff4efe4)); g.fillPath (p, rot);
            break;
        }
        case KnobChrome:
        {
            juce::ColourGradient ch (juce::Colour (0xffffffff), c.x - R * 0.5f, c.y - R * 0.9f,
                                     juce::Colour (0xff55585d), c.x + R * 0.6f, c.y + R, true);
            ch.addColour (0.35, juce::Colour (0xffb9bcc1));
            ch.addColour (0.7, juce::Colour (0xff7d8086));
            disc (R, ch);
            knurl (g, c, R, 40, juce::Colour (0xffffffff), juce::Colour (0xff3d3f43));
            const float cr = R * 0.78f;
            juce::ColourGradient top (juce::Colour (0xffe9eaec), c.x, c.y - cr, juce::Colour (0xff8a8d92), c.x, c.y + cr, false);
            disc (cr, top);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.fillEllipse (c.x - cr * 0.55f, c.y - cr * 0.75f, cr * 0.7f, cr * 0.35f);
            juce::Path p; p.addRoundedRectangle (-1.4f, -cr + 2.0f, 2.8f, cr * 0.6f, 1.0f);
            g.setColour (juce::Colour (0xff111111)); g.fillPath (p, rot);
            break;
        }
        case KnobBlackPointer: // preto serrilhado, topo brilhante e linha branca
        {
            disc (R, juce::ColourGradient (juce::Colour (0xff3a3a3e), c.x, c.y - R, juce::Colour (0xff040405), c.x, c.y + R, false));
            knurl (g, c, R, 34, juce::Colour (0xff6a6a70), juce::Colour (0xff050505));
            const float cr = R * 0.74f;
            juce::ColourGradient top (juce::Colour (0xff2e2e33), c.x - cr * 0.5f, c.y - cr, juce::Colour (0xff070708), c.x + cr * 0.4f, c.y + cr, false);
            disc (cr, top);
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.drawEllipse (c.x - cr, c.y - cr, cr * 2, cr * 2, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.fillEllipse (c.x - cr * 0.6f, c.y - cr * 0.85f, cr * 0.9f, cr * 0.45f);
            juce::Path p; p.addRoundedRectangle (-1.4f, -cr + 1.0f, 2.8f, cr * 0.85f, 1.2f);
            g.setColour (juce::Colour (0xfff2f2f2)); g.fillPath (p, rot);
            break;
        }
        case KnobBrass: // tampa "sino" cromada com tom dourado
        {
            juce::ColourGradient ch (juce::Colour (0xfffff6dc), c.x - R * 0.5f, c.y - R * 0.9f,
                                     juce::Colour (0xff4a3d26), c.x + R * 0.6f, c.y + R, true);
            ch.addColour (0.35, juce::Colour (0xffb59a5e));
            ch.addColour (0.7, juce::Colour (0xff7c6a48));
            disc (R, ch);
            knurl (g, c, R, 40, juce::Colour (0xfffff3d0), juce::Colour (0xff3a3020));
            const float cr = R * 0.76f;
            juce::ColourGradient top (juce::Colour (0xfffbf3dc), c.x - cr * 0.6f, c.y - cr, juce::Colour (0xff3a2e18), c.x + cr * 0.6f, c.y + cr, false);
            top.addColour (0.4, juce::Colour (0xffc2ab78));
            top.addColour (0.6, juce::Colour (0xff7a6438));
            disc (cr, top);
            g.setColour (juce::Colours::black.withAlpha (0.1f));
            for (float k = 0.25f; k < 1.0f; k += 0.15f)
                g.drawEllipse (c.x - cr * k, c.y - cr * k, cr * 2 * k, cr * 2 * k, 0.7f);
            g.setColour (juce::Colours::white.withAlpha (0.4f));
            g.fillEllipse (c.x - cr * 0.55f, c.y - cr * 0.8f, cr * 0.7f, cr * 0.32f);
            juce::Path p; p.addRoundedRectangle (-1.2f, -cr + 1.5f, 2.4f, cr * 0.55f, 1.0f);
            g.setColour (juce::Colour (0xff1a140a)); g.fillPath (p, rot);
            break;
        }
        case KnobCream: // knob creme com ponteiro escuro (pedais)
        {
            disc (R, juce::ColourGradient (juce::Colour (0xfff6efdc), c.x - R * 0.4f, c.y - R, juce::Colour (0xffb7a98a), c.x + R * 0.5f, c.y + R, false));
            knurl (g, c, R, 30, juce::Colour (0xfffffaee), juce::Colour (0xff9a8d70));
            const float cr = R * 0.72f;
            disc (cr, juce::ColourGradient (juce::Colour (0xfffbf6ea), c.x, c.y - cr, juce::Colour (0xffd2c6aa), c.x, c.y + cr, false));
            juce::Path p; p.addRoundedRectangle (-1.4f, -R + 2.0f, 2.8f, R * 0.62f, 1.2f);
            g.setColour (juce::Colour (0xff1f1a12)); g.fillPath (p, rot);
            break;
        }
        case KnobBlackChrome:
        default:
        {
            disc (R, juce::ColourGradient (juce::Colour (0xff3b3b3f), c.x, c.y - R, juce::Colour (0xff050506), c.x, c.y + R, false));
            knurl (g, c, R, 30, juce::Colour (0xff5c5c62), juce::Colour (0xff060606));
            const float cr = R * 0.42f;
            juce::ColourGradient ch (juce::Colour (0xffffffff), c.x - cr * 0.5f, c.y - cr * 0.8f,
                                     juce::Colour (0xff5d6066), c.x + cr * 0.6f, c.y + cr, true);
            disc (cr, ch);
            juce::Path p; p.addRoundedRectangle (-1.4f, -R + 2.0f, 2.8f, R - cr - 1.0f, 1.0f);
            g.setColour (juce::Colours::white); g.fillPath (p, rot);
            break;
        }
    }
}

void ValvedLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                   float startAngle, float endAngle, juce::Slider& s)
{
    const int style = (int) s.getProperties().getWithDefault ("knobStyle", (int) KnobModern);
    auto r = juce::Rectangle<int> (x, y, w, h).toFloat();
    if (style == KnobModern)
        drawModernKnob (g, r, pos, startAngle, endAngle);
    else
    {
        const float d = juce::jmin (r.getWidth(), r.getHeight());
        drawAmpKnob (g, r.withSizeKeepingCentre (d, d), startAngle + pos * (endAngle - startAngle), style);
    }
}

void ValvedLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                       bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    const bool tab = b.getProperties().getWithDefault ("tab", false);
    if (tab)
    {
        if (on)
        {
            g.setColour (accent);
            g.fillRect (r.removeFromBottom (3.0f));
        }
        else if (highlighted)
        {
            g.setColour (line);
            g.fillRect (r.removeFromBottom (2.0f));
        }
        return;
    }
    juce::Colour c = on ? accent : panelHi;
    if (highlighted && ! on) c = c.brighter (0.1f);
    if (down) c = c.darker (0.15f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 5.0f);
    if (! on)
    {
        g.setColour (line.brighter (0.2f));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }
}

void ValvedLook::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool highlighted, bool)
{
    const bool on = b.getToggleState();
    const bool tab = b.getProperties().getWithDefault ("tab", false);
    g.setFont (Fonts::condensedBold (tab ? 18.0f : 17.0f));
    juce::Colour c = on ? (tab ? text : juce::Colours::black) : (highlighted ? text : textDim);
    g.setColour (c);
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

//==============================================================================
void ToggleSwitch::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();
    const auto c = juce::Point<float> (r.getCentreX(), r.getCentreY() + 6.0f);
    const float nut = juce::jmin (r.getWidth(), r.getHeight()) * 0.22f;

    // LED
    const float ledR = 4.0f;
    const juce::Point<float> lp (r.getCentreX(), r.getY() + ledR + 1.0f);
    if (on)
    {
        g.setColour (led.withAlpha (0.35f));
        g.fillEllipse (lp.x - ledR * 2.5f, lp.y - ledR * 2.5f, ledR * 5.0f, ledR * 5.0f);
    }
    g.setColour (on ? led : led.darker (0.85f));
    g.fillEllipse (lp.x - ledR, lp.y - ledR, ledR * 2, ledR * 2);

    // Porca cromada
    juce::ColourGradient ng (juce::Colour (0xfff4f4f4), c.x - nut, c.y - nut, juce::Colour (0xff55585e), c.x + nut, c.y + nut, false);
    g.setGradientFill (ng);
    g.fillEllipse (c.x - nut, c.y - nut, nut * 2, nut * 2);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (c.x - nut, c.y - nut, nut * 2, nut * 2, 1.0f);

    // Alavanca
    const float len = nut * 2.3f;
    const float dir = on ? -1.0f : 1.0f;
    juce::Path lever;
    lever.startNewSubPath (c.x - nut * 0.32f, c.y);
    lever.lineTo (c.x + nut * 0.32f, c.y);
    lever.lineTo (c.x + nut * 0.42f, c.y + dir * len);
    lever.lineTo (c.x - nut * 0.42f, c.y + dir * len);
    lever.closeSubPath();
    juce::ColourGradient lg (juce::Colour (0xff6d7076), c.x - nut * 0.4f, c.y, juce::Colour (0xffffffff), c.x + nut * 0.1f, c.y, false);
    lg.addColour (1.0, juce::Colour (0xff7a7d83));
    g.setGradientFill (lg);
    g.fillPath (lever);
    g.fillEllipse (c.x - nut * 0.5f, c.y + dir * len - nut * 0.5f, nut, nut);
    if (highlighted)
    {
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.fillEllipse (c.x - nut * 1.6f, c.y - nut * 1.6f, nut * 3.2f, nut * 3.2f);
    }
}

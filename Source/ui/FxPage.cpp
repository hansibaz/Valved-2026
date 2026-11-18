#include "FxPage.h"
#include "../PluginProcessor.h"

using namespace ui;

namespace
{
    constexpr float knobStart = juce::MathConstants<float>::pi * (7.0f / 6.0f);
    constexpr float knobEnd   = juce::MathConstants<float>::pi * (17.0f / 6.0f);
    juce::Path rounded (juce::Rectangle<float> r, float radius) { juce::Path p; p.addRoundedRectangle (r, radius); return p; }

    void led (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col, bool lit)
    {
        if (lit)
        {
            juce::ColourGradient glow (col.withAlpha (0.55f), c.x, c.y, col.withAlpha (0.0f), c.x + r * 4.5f, c.y, true);
            g.setGradientFill (glow);
            g.fillEllipse (c.x - r * 4.5f, c.y - r * 4.5f, r * 9.0f, r * 9.0f);
        }
        g.setColour (juce::Colour (0xff0b0b0c));
        g.fillEllipse (c.x - r - 1.2f, c.y - r - 1.2f, (r + 1.2f) * 2, (r + 1.2f) * 2);
        juce::ColourGradient lg (lit ? col.brighter (0.8f) : col.darker (0.85f), c.x - r * 0.3f, c.y - r * 0.4f,
                                 lit ? col.darker (0.2f) : col.darker (0.95f), c.x + r, c.y + r, true);
        g.setGradientFill (lg);
        g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
    }
}

//==============================================================================
TypeSelector::TypeSelector (juce::RangedAudioParameter* p, juce::StringArray n, juce::Colour l)
    : param (p), names (std::move (n)), ledColour (l)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float> TypeSelector::itemRect (int i) const
{
    const int rows = (names.size() + 1) / 2;
    const float w = (float) getWidth() * 0.5f, h = (float) getHeight() / (float) rows;
    return { (float) (i / rows) * w, (float) (i % rows) * h, w, h };
}

void TypeSelector::paint (juce::Graphics& g)
{
    for (int i = 0; i < names.size(); ++i)
    {
        const auto r = itemRect (i);
        const bool sel = i == selected;
        led (g, { r.getX() + 9.0f, r.getCentreY() }, 3.2f, ledColour, sel);
        g.setFont (Fonts::condensedBold (15.0f));
        g.setColour (sel ? juce::Colours::white : juce::Colour (0xffb9bec8).withAlpha (0.75f));
        g.drawText (names[i].toUpperCase(), r.withTrimmedLeft (22.0f), juce::Justification::centredLeft);
    }
}

void TypeSelector::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < names.size(); ++i)
        if (itemRect (i).contains (e.position))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 ((float) i));
            param->endChangeGesture();
            setSelected (i);
            break;
        }
}

//==============================================================================
FxUnit::FxUnit (ValvedProcessor& p, Kind k) : proc (p), kind (k)
{
    auto& s = p.apvts;
    accentCol = kind == DelayUnit ? juce::Colour (0xffffb23a) : juce::Colour (0xff53d6ff);

    if (kind == DelayUnit)
    {
        ids = { ParamID::dlyTime, ParamID::dlyRepeats, ParamID::dlyMix, ParamID::dlyTone, ParamID::dlyMod, ParamID::dlyParam };
        labels = { "TIME", "REPEATS", "MIX", "TONE", "MOD", "" };
        selector = std::make_unique<TypeSelector> (s.getParameter (ParamID::dlyType), vlv::StereoDelay::typeNames(), accentCol);
        onAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, ParamID::dlyOn, onSwitch);
        addAndMakeVisible (tapSwitch);
        tapSwitch.onClick = [this] { tap(); };
        addAndMakeVisible (syncSwitch);
        syncAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, ParamID::dlySync, syncSwitch);
        division.addItemList (ValvedProcessor::divisionNames(), 1);
        addAndMakeVisible (division);
        divAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, ParamID::dlyDiv, division);
    }
    else
    {
        ids = { ParamID::revDecay, ParamID::revPre, ParamID::revMix, ParamID::revTone, ParamID::revMod, ParamID::revParam };
        labels = { "DECAY", "PRE-DELAY", "MIX", "TONE", "MOD", "" };
        selector = std::make_unique<TypeSelector> (s.getParameter (ParamID::revType), vlv::AmbientReverb::typeNames(), accentCol);
        onAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, ParamID::revOn, onSwitch);
    }
    addAndMakeVisible (*selector);
    addAndMakeVisible (onSwitch);

    for (auto& id : ids)
    {
        auto* kn = knobs.add (new juce::Slider());
        kn->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        kn->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        kn->setRotaryParameters (knobStart, knobEnd, true);
        kn->setPopupDisplayEnabled (true, true, nullptr);
        kn->setMouseDragSensitivity (220);
        kn->getProperties().set ("knobStyle", (int) KnobBlackPointer);
        addAndMakeVisible (kn);
        knobAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, *kn));
        if (auto* prm = s.getParameter (id))
            kn->setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    }
}

void FxUnit::resized()
{
    const auto r = getLocalBounds().toFloat().reduced (4.0f, 6.0f);
    const float W = r.getWidth();
    display = { r.getX() + 22.0f, r.getY() + 66.0f, W * 0.44f, 104.0f };
    selector->setBounds (juce::Rectangle<float> (display.getRight() + 26.0f, display.getY() - 2.0f,
                                                 r.getRight() - display.getRight() - 46.0f, 108.0f).toNearestInt());
    panel = { r.getX() + 16.0f, display.getBottom() + 22.0f, W - 32.0f, 128.0f };

    const float step = (panel.getWidth() - 20.0f) / 6.0f;
    const float kd = juce::jmin (62.0f, step - 6.0f);
    for (int i = 0; i < knobs.size(); ++i)
        knobs[i]->setBounds (juce::Rectangle<float> (kd, kd).withCentre ({ panel.getX() + 10.0f + step * ((float) i + 0.5f), panel.getY() + 58.0f }).toNearestInt());

    const float fy = r.getBottom() - 70.0f;
    const float fsz = 76.0f;
    if (kind == DelayUnit)
    {
        onSwitch.setBounds (juce::Rectangle<float> (fsz, fsz).withCentre ({ r.getX() + W * 0.62f, fy }).toNearestInt());
        tapSwitch.setBounds (juce::Rectangle<float> (fsz, fsz).withCentre ({ r.getX() + W * 0.85f, fy }).toNearestInt());
        syncSwitch.setBounds (juce::Rectangle<float> (34.0f, 46.0f).withCentre ({ r.getX() + 52.0f, fy + 6.0f }).toNearestInt());
        division.setBounds (juce::Rectangle<float> (100.0f, 30.0f).withCentre ({ r.getX() + 148.0f, fy + 10.0f }).toNearestInt());
    }
    else
        onSwitch.setBounds (juce::Rectangle<float> (fsz, fsz).withCentre ({ r.getX() + W * 0.75f, fy }).toNearestInt());
    body = {};
}

void FxUnit::renderBody()
{
    const float scale = 2.0f;
    body = juce::Image (juce::Image::ARGB, juce::jmax (1, getWidth() * 2), juce::jmax (1, getHeight() * 2), true);
    juce::Graphics g (body);
    g.addTransform (juce::AffineTransform::scale (scale));

    const auto r = getLocalBounds().toFloat().reduced (4.0f, 6.0f);
    const float R = 18.0f;
    const auto shape = rounded (r, R);
    const juce::Colour top = kind == DelayUnit ? juce::Colour (0xff2b3650) : juce::Colour (0xff1d3f45);
    const juce::Colour bottom = kind == DelayUnit ? juce::Colour (0xff121725) : juce::Colour (0xff0b1c1f);

    juce::DropShadow (juce::Colours::black.withAlpha (0.85f), 28, { 0, 14 }).drawForPath (g, shape);
    g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(), bottom, r.getX() + r.getWidth() * 0.4f, r.getBottom(), false));
    g.fillPath (shape);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (shape);
        juce::Random rng (kind == DelayUnit ? 7 : 9);
        for (float y = r.getY(); y < r.getBottom(); y += 0.75f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.035f * rng.nextFloat()));
            g.fillRect (r.getX(), y, r.getWidth(), 0.5f);
        }
        juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.14f), r.getX(), r.getY(), juce::Colours::transparentWhite, r.getX() + r.getWidth() * 0.5f, r.getY() + r.getHeight() * 0.45f, false);
        g.setGradientFill (sheen);
        g.fillRect (r);
    }
    g.setColour (juce::Colours::white.withAlpha (0.2f));
    g.strokePath (rounded (r.reduced (1.0f), R), juce::PathStrokeType (1.2f));

    // Cabeçalho
    g.setColour (accentCol);
    g.setFont (Fonts::condensedBold (13.0f));
    g.drawText ("V A L V E D", juce::Rectangle<float> (r.getX() + 24.0f, r.getY() + 14.0f, 200.0f, 16.0f), juce::Justification::centredLeft);
    g.setColour (juce::Colours::white);
    g.setFont (Fonts::wide (24.0f));
    g.drawText (kind == DelayUnit ? "DELAY" : "REVERB", juce::Rectangle<float> (r.getX() + 22.0f, r.getY() + 28.0f, 260.0f, 30.0f), juce::Justification::centredLeft);
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.setFont (Fonts::condensed (14.0f));
    g.drawText (kind == DelayUnit ? "STEREO MULTI-MODE DELAY" : "AMBIENT MULTI-REVERB",
                juce::Rectangle<float> (r.getRight() - 260.0f, r.getY() + 30.0f, 236.0f, 24.0f), juce::Justification::centredRight);

    // Moldura do display
    const auto dp = rounded (display.expanded (5.0f), 9.0f);
    g.setColour (juce::Colour (0xff0a0b0e));
    g.fillPath (dp);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.strokePath (dp, juce::PathStrokeType (1.0f));

    // Rebaixo do seletor
    auto selArea = selector->getBounds().toFloat().expanded (8.0f, 6.0f);
    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.fillRoundedRectangle (selArea, 8.0f);

    // Painel dos knobs
    const auto pp = rounded (panel, 12.0f);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillPath (pp);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.strokePath (pp, juce::PathStrokeType (1.0f));
    for (auto* kn : knobs)
    {
        const auto kr = kn->getBounds().toFloat();
        const auto c = kr.getCentre();
        const float kR = kr.getWidth() * 0.5f;
        for (int t = 0; t <= 10; ++t)
        {
            const float a = knobStart + (knobEnd - knobStart) * (float) t / 10.0f;
            g.setColour (juce::Colours::white.withAlpha (t % 5 == 0 ? 0.6f : 0.25f));
            g.drawLine (c.x + std::sin (a) * kR * 1.08f, c.y - std::cos (a) * kR * 1.08f,
                        c.x + std::sin (a) * kR * (t % 5 == 0 ? 1.2f : 1.15f), c.y - std::cos (a) * kR * (t % 5 == 0 ? 1.2f : 1.15f), 1.0f);
        }
    }

    // Rótulos dos footswitches
    g.setColour (juce::Colours::white.withAlpha (0.75f));
    g.setFont (Fonts::condensedBold (14.0f));
    auto under = [&g] (juce::Component& c, const char* t)
    {
        g.drawText (t, juce::Rectangle<float> (120.0f, 16.0f).withCentre ({ (float) c.getBounds().getCentreX(), (float) c.getBottom() + 10.0f }), juce::Justification::centred);
    };
    under (onSwitch, "ON / OFF");
    if (kind == DelayUnit)
    {
        under (tapSwitch, "TAP");
        g.drawText ("SYNC", juce::Rectangle<float> (60.0f, 16.0f).withCentre ({ (float) syncSwitch.getBounds().getCentreX(), (float) syncSwitch.getBottom() + 10.0f }), juce::Justification::centred);
        g.drawText ("DIVISION", juce::Rectangle<float> (100.0f, 16.0f).withCentre ({ (float) division.getBounds().getCentreX(), (float) division.getY() - 12.0f }), juce::Justification::centred);
    }
    else
    {
        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.setFont (Fonts::condensed (13.0f));
        g.drawText ("Trails: the tail keeps ringing when you switch off",
                    juce::Rectangle<float> (r.getX() + 24.0f, (float) onSwitch.getBounds().getCentreY() - 10.0f, r.getWidth() * 0.5f, 20.0f),
                    juce::Justification::centredLeft);
    }

    // Parafusos
    for (auto pt : { r.getTopLeft().translated (11, 11), r.getTopRight().translated (-11, 11),
                     r.getBottomLeft().translated (11, -11), r.getBottomRight().translated (-11, -11) })
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe0e2e6), pt.x - 3, pt.y - 3, juce::Colour (0xff45484e), pt.x + 3, pt.y + 3, false));
        g.fillEllipse (pt.x - 3.5f, pt.y - 3.5f, 7.0f, 7.0f);
    }
}

void FxUnit::paint (juce::Graphics& g)
{
    if (body.isNull() || body.getWidth() != getWidth() * 2) renderBody();
    g.drawImage (body, getLocalBounds().toFloat());

    // Display
    {
        juce::ColourGradient glass (juce::Colour (0xff10141a), display.getX(), display.getY(), juce::Colour (0xff05070a), display.getX(), display.getBottom(), false);
        g.setGradientFill (glass);
        g.fillRoundedRectangle (display, 6.0f);
        const auto ink = onState ? accentCol : accentCol.withAlpha (0.35f);
        g.setColour (ink.withAlpha (onState ? 0.08f : 0.03f));
        g.fillRoundedRectangle (display, 6.0f);
        g.setColour (ink);
        g.setFont (Fonts::wide (20.0f));
        g.drawFittedText (shown1, display.reduced (14.0f, 0).withTrimmedTop (16.0f).withHeight (32.0f).toNearestInt(), juce::Justification::centredLeft, 1);
        g.setFont (Fonts::condensedMedium (16.0f));
        g.setColour (ink.withAlpha (onState ? 0.85f : 0.3f));
        g.drawFittedText (shown2, display.reduced (14.0f, 0).withTrimmedTop (56.0f).withHeight (22.0f).toNearestInt(), juce::Justification::centredLeft, 1);
        g.setFont (Fonts::condensed (12.0f));
        g.drawText (onState ? "ON" : "BYPASS", display.reduced (12.0f, 8.0f), juce::Justification::topRight);
        juce::ColourGradient glare (juce::Colours::white.withAlpha (0.06f), display.getX(), display.getY(), juce::Colours::transparentWhite, display.getX(), display.getY() + display.getHeight() * 0.5f, false);
        g.setGradientFill (glare);
        g.fillRoundedRectangle (display, 6.0f);
    }

    // Nomes dos knobs (o último muda conforme o tipo)
    g.setFont (Fonts::condensedBold (14.0f));
    for (int i = 0; i < knobs.size(); ++i)
    {
        const auto kr = knobs[i]->getBounds().toFloat();
        const juce::String t = i == knobs.size() - 1 ? specialLabel : labels[i];
        g.setColour (i == knobs.size() - 1 ? accentCol : juce::Colours::white.withAlpha (0.85f));
        g.drawText (t, juce::Rectangle<float> (110.0f, 18.0f).withCentre ({ kr.getCentreX(), kr.getBottom() + 18.0f }), juce::Justification::centred);
    }

    // LEDs dos footswitches
    led (g, { (float) onSwitch.getBounds().getCentreX(), (float) onSwitch.getY() - 14.0f }, 4.5f, accentCol, onState);
    if (kind == DelayUnit)
        led (g, { (float) tapSwitch.getBounds().getCentreX(), (float) tapSwitch.getY() - 14.0f }, 4.5f, juce::Colour (0xffff3a2a), tapLed);
}

juce::String FxUnit::displayLine1() const
{
    if (kind == DelayUnit)
        return vlv::StereoDelay::typeNames()[(int) proc.apvts.getRawParameterValue (ParamID::dlyType)->load()].toUpperCase();
    return vlv::AmbientReverb::typeNames()[(int) proc.apvts.getRawParameterValue (ParamID::revType)->load()].toUpperCase();
}

juce::String FxUnit::displayLine2() const
{
    auto& s = proc.apvts;
    if (kind == DelayUnit)
    {
        const float ms = proc.getEffectiveDelayMs();
        const juce::String mix = "MIX " + juce::String ((int) s.getRawParameterValue (ParamID::dlyMix)->load()) + "%";
        if (s.getRawParameterValue (ParamID::dlySync)->load() > 0.5f)
            return ValvedProcessor::divisionNames()[(int) s.getRawParameterValue (ParamID::dlyDiv)->load()] + "  @ "
                   + juce::String (proc.getBpm(), 0) + " BPM  =  " + juce::String (ms, 0) + " ms   " + mix;
        return juce::String (ms, 0) + " ms   " + mix;
    }
    const int t = (int) s.getRawParameterValue (ParamID::revType)->load();
    const float secs = vlv::AmbientReverb::decaySeconds (t, s.getRawParameterValue (ParamID::revDecay)->load());
    return "DECAY " + juce::String (secs, 1) + " s   PRE " + juce::String ((int) s.getRawParameterValue (ParamID::revPre)->load())
           + " ms   MIX " + juce::String ((int) s.getRawParameterValue (ParamID::revMix)->load()) + "%";
}

void FxUnit::refresh()
{
    auto& s = proc.apvts;
    const int type = (int) s.getRawParameterValue (kind == DelayUnit ? ParamID::dlyType : ParamID::revType)->load();
    selector->setSelected (type);
    const bool on = s.getRawParameterValue (kind == DelayUnit ? ParamID::dlyOn : ParamID::revOn)->load() > 0.5f;
    const juce::String l1 = displayLine1(), l2 = displayLine2();
    const juce::String sp = kind == DelayUnit ? vlv::StereoDelay::paramName (type) : vlv::AmbientReverb::paramName (type);

    bool tl = false;
    if (kind == DelayUnit)
    {
        const bool sync = s.getRawParameterValue (ParamID::dlySync)->load() > 0.5f;
        knobs[0]->setEnabled (! sync);
        knobs[0]->setAlpha (sync ? 0.4f : 1.0f);
        division.setEnabled (sync);
        division.setAlpha (sync ? 1.0f : 0.45f);
        const double period = juce::jmax (40.0, (double) proc.getEffectiveDelayMs());
        tl = on && std::fmod (juce::Time::getMillisecondCounterHiRes(), period) < juce::jmin (90.0, period * 0.4);
    }

    if (l1 != shown1 || l2 != shown2 || sp != specialLabel || on != onState || tl != tapLed)
    {
        shown1 = l1; shown2 = l2; specialLabel = sp; onState = on; tapLed = tl;
        repaint();
    }
}

void FxUnit::tap()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (now - lastTapMs > 2500.0) taps.clear();
    else taps.push_back (now - lastTapMs);
    lastTapMs = now;
    if (taps.size() > 4) taps.erase (taps.begin());
    if (taps.empty()) return;
    double avg = 0.0;
    for (auto t : taps) avg += t;
    avg /= (double) taps.size();
    if (auto* sync = proc.apvts.getParameter (ParamID::dlySync)) sync->setValueNotifyingHost (0.0f);
    if (auto* prm = proc.apvts.getParameter (ParamID::dlyTime))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) juce::jlimit (20.0, 2000.0, avg)));
        prm->endChangeGesture();
    }
}

//==============================================================================
void EqFader::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float yTop = (float) getPositionOfValue (getMaximum()), yBot = (float) getPositionOfValue (getMinimum());
    const float y0 = (float) getPositionOfValue (0.0), yv = (float) getPositionOfValue (getValue());
    const float cx = b.getCentreX();
    // trilho
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.fillRoundedRectangle (cx - 2.5f, yTop - 2.0f, 5.0f, yBot - yTop + 4.0f, 2.5f);
    // barra iluminada do 0 até o valor
    g.setColour (led.withAlpha (isEnabled() ? 0.85f : 0.3f));
    g.fillRect (cx - 1.0f, juce::jmin (y0, yv), 2.0f, std::abs (yv - y0));
    // capa do fader
    auto cap = juce::Rectangle<float> (juce::jmin (b.getWidth() - 2.0f, 26.0f), 14.0f).withCentre ({ cx, yv });
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (cap.translated (0.0f, 2.0f), 3.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe9ebef), cap.getX(), cap.getY(), juce::Colour (0xff6c7078), cap.getX(), cap.getBottom(), false));
    g.fillRoundedRectangle (cap, 3.0f);
    g.setColour (led.withAlpha (isEnabled() ? 1.0f : 0.4f));
    g.fillRect (cap.getX() + 3.0f, cap.getCentreY() - 1.0f, cap.getWidth() - 6.0f, 2.0f);
}

EqUnit::EqUnit (ValvedProcessor& p) : proc (p)
{
    auto& s = p.apvts;
    const juce::Colour led (0xff7dff9a);
    auto add = [&] (juce::OwnedArray<EqFader>& arr, const juce::String& id)
    {
        auto* f = arr.add (new EqFader (led));
        addChildComponent (f);
        att.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, *f));
        f->setDoubleClickReturnValue (true, 0.0);
    };
    for (int b = 0; b < 7; ++b) add (vFaders, ParamID::eqBand (0, b));
    for (int b = 0; b < 5; ++b) add (mFaders, ParamID::eqBand (1, b));
    addAndMakeVisible (level);
    att.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, ParamID::eqLevel, level));
    level.setDoubleClickReturnValue (true, 0.0);

    for (auto* b : { &modeV, &modeM })
    {
        addAndMakeVisible (b);
        b->setClickingTogglesState (false);
    }
    modeV.setConnectedEdges (juce::Button::ConnectedOnRight);
    modeM.setConnectedEdges (juce::Button::ConnectedOnLeft);
    modeV.setTooltip ("Valved 7-band: frequencies chosen for guitar");
    modeM.setTooltip ("Mark 5-band: the classic 80 / 240 / 750 / 2200 / 6600 Hz graphic EQ");
    auto setMode = [this] (int m)
    {
        if (auto* prm = proc.apvts.getParameter (ParamID::eqMode))
        {
            prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 ((float) m)); prm->endChangeGesture();
        }
        refresh();
    };
    modeV.onClick = [setMode] { setMode (0); };
    modeM.onClick = [setMode] { setMode (1); };

    addAndMakeVisible (onSwitch);
    onAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, ParamID::eqOn, onSwitch);
    refresh();
}

void EqUnit::refresh()
{
    const int m = (int) proc.apvts.getRawParameterValue (ParamID::eqMode)->load();
    const bool on = proc.apvts.getRawParameterValue (ParamID::eqOn)->load() > 0.5f;
    if (m != mode || ! modeV.getToggleState() == ! modeM.getToggleState())
    {
        mode = m;
        for (auto* f : vFaders) f->setVisible (mode == 0);
        for (auto* f : mFaders) f->setVisible (mode == 1);
        modeV.setToggleState (mode == 0, juce::dontSendNotification);
        modeM.setToggleState (mode == 1, juce::dontSendNotification);
        resized();
        repaint();
    }
    if (on != onState) { onState = on; repaint(); }
}

void EqUnit::resized()
{
    const auto r = getLocalBounds().toFloat().reduced (4.0f, 6.0f);
    const float W = r.getWidth();
    auto mb = juce::Rectangle<float> (r.getX() + 18.0f, r.getY() + 70.0f, W - 36.0f, 28.0f);
    modeV.setBounds (mb.removeFromLeft (mb.getWidth() * 0.5f).toNearestInt());
    modeM.setBounds (mb.toNearestInt());
    faderArea = { r.getX() + 14.0f, r.getY() + 112.0f, W - 28.0f, 240.0f };
    auto fa = faderArea.reduced (6.0f, 24.0f);
    fa.removeFromLeft (20.0f);   // espaço da escala em dB
    const float levelW = 34.0f;
    auto lv = fa.removeFromRight (levelW);
    fa.removeFromRight (8.0f);
    auto place = [&] (juce::OwnedArray<EqFader>& arr)
    {
        const float w = fa.getWidth() / (float) arr.size();
        for (int i = 0; i < arr.size(); ++i)
            arr[i]->setBounds (juce::Rectangle<float> (fa.getX() + w * (float) i, fa.getY(), w, fa.getHeight()).toNearestInt());
    };
    place (vFaders); place (mFaders);
    level.setBounds (lv.toNearestInt());
    const float fsz = 76.0f;
    onSwitch.setBounds (juce::Rectangle<float> (fsz, fsz).withCentre ({ r.getCentreX(), r.getBottom() - 70.0f }).toNearestInt());
    body = {};
}

void EqUnit::renderBody()
{
    body = juce::Image (juce::Image::ARGB, juce::jmax (1, getWidth() * 2), juce::jmax (1, getHeight() * 2), true);
    juce::Graphics g (body);
    g.addTransform (juce::AffineTransform::scale (2.0f));
    const auto r = getLocalBounds().toFloat().reduced (4.0f, 6.0f);
    const float R = 18.0f;
    const auto shape = rounded (r, R);
    juce::DropShadow (juce::Colours::black.withAlpha (0.85f), 28, { 0, 14 }).drawForPath (g, shape);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3d42), r.getX(), r.getY(), juce::Colour (0xff15171a), r.getX() + r.getWidth() * 0.4f, r.getBottom(), false));
    g.fillPath (shape);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (shape);
        juce::Random rng (11);
        for (float y = r.getY(); y < r.getBottom(); y += 0.75f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.035f * rng.nextFloat()));
            g.fillRect (r.getX(), y, r.getWidth(), 0.5f);
        }
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.14f), r.getX(), r.getY(), juce::Colours::transparentWhite, r.getX() + r.getWidth() * 0.5f, r.getY() + r.getHeight() * 0.45f, false));
        g.fillRect (r);
    }
    g.setColour (juce::Colours::white.withAlpha (0.2f));
    g.strokePath (rounded (r.reduced (1.0f), R), juce::PathStrokeType (1.2f));

    g.setColour (juce::Colour (0xff7dff9a));
    g.setFont (Fonts::condensedBold (13.0f));
    g.drawText ("V A L V E D", juce::Rectangle<float> (r.getX() + 24.0f, r.getY() + 14.0f, 200.0f, 16.0f), juce::Justification::centredLeft);
    g.setColour (juce::Colours::white);
    g.setFont (Fonts::wide (24.0f));
    g.drawText ("EQ", juce::Rectangle<float> (r.getX() + 22.0f, r.getY() + 28.0f, 100.0f, 30.0f), juce::Justification::centredLeft);
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.setFont (Fonts::condensed (14.0f));
    g.drawText ("GRAPHIC EQ", juce::Rectangle<float> (r.getRight() - 140.0f, r.getY() + 30.0f, 116.0f, 24.0f), juce::Justification::centredRight);

    // painel dos faders com escala
    const auto pp = rounded (faderArea, 12.0f);
    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.fillPath (pp);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.strokePath (pp, juce::PathStrokeType (1.0f));
    auto& arr = mode == 1 ? mFaders : vFaders;
    if (arr.size() > 0)
    {
        auto* f0 = arr[0];
        const float yTop = (float) f0->getY() + (float) f0->getPositionOfValue (12.0);
        const float yMid = (float) f0->getY() + (float) f0->getPositionOfValue (0.0);
        const float yBot = (float) f0->getY() + (float) f0->getPositionOfValue (-12.0);
        g.setFont (Fonts::condensed (11.0f));
        for (auto [y, t] : { std::pair<float, const char*> { yTop, "+12" }, { yMid, "0" }, { yBot, "-12" } })
        {
            g.setColour (juce::Colours::white.withAlpha (t[0] == '0' ? 0.22f : 0.12f));
            g.fillRect (faderArea.getX() + 26.0f, y - 0.5f, faderArea.getWidth() - 32.0f, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.45f));
            g.drawText (t, juce::Rectangle<float> (faderArea.getX() + 2.0f, y - 7.0f, 22.0f, 14.0f), juce::Justification::centredRight);
        }
        // rótulos de frequência
        const char* vl[7] = { "80", "200", "400", "800", "1k6", "3k2", "6k4" };
        const char* ml[5] = { "80", "240", "750", "2200", "6600" };
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.setFont (Fonts::condensedBold (mode == 1 ? 12.0f : 11.0f));
        for (int i = 0; i < arr.size(); ++i)
        {
            const auto fb = arr[i]->getBounds().toFloat();
            g.drawText (mode == 1 ? ml[i] : vl[i], juce::Rectangle<float> (fb.getX() - 2.0f, faderArea.getBottom() - 20.0f, fb.getWidth() + 4.0f, 16.0f), juce::Justification::centred);
        }
        const auto lb = level.getBounds().toFloat();
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawText ("LEVEL", juce::Rectangle<float> (lb.getX() - 10.0f, faderArea.getBottom() - 20.0f, lb.getWidth() + 20.0f, 16.0f), juce::Justification::centred);
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (Fonts::condensed (11.0f));
        g.drawText ("dB", juce::Rectangle<float> (faderArea.getX() + 2.0f, faderArea.getY() + 4.0f, 22.0f, 14.0f), juce::Justification::centredRight);
    }
    g.setColour (juce::Colours::white.withAlpha (0.75f));
    g.setFont (Fonts::condensedBold (14.0f));
    g.drawText ("ON / OFF", juce::Rectangle<float> (120.0f, 16.0f).withCentre ({ (float) onSwitch.getBounds().getCentreX(), (float) onSwitch.getBottom() + 10.0f }), juce::Justification::centred);
    for (auto pt : { r.getTopLeft().translated (11, 11), r.getTopRight().translated (-11, 11),
                     r.getBottomLeft().translated (11, -11), r.getBottomRight().translated (-11, -11) })
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe0e2e6), pt.x - 3, pt.y - 3, juce::Colour (0xff45484e), pt.x + 3, pt.y + 3, false));
        g.fillEllipse (pt.x - 3.5f, pt.y - 3.5f, 7.0f, 7.0f);
    }
}

void EqUnit::paint (juce::Graphics& g)
{
    if (body.isNull() || body.getWidth() != getWidth() * 2) renderBody();
    g.drawImage (body, getLocalBounds().toFloat());
    // LED do ON
    const auto c = juce::Point<float> ((float) onSwitch.getBounds().getCentreX(), (float) onSwitch.getY() - 14.0f);
    const juce::Colour led (0xff7dff9a);
    if (onState) { g.setColour (led.withAlpha (0.35f)); g.fillEllipse (c.x - 9.0f, c.y - 9.0f, 18.0f, 18.0f); }
    g.setColour (onState ? led : juce::Colour (0xff1d3324));
    g.fillEllipse (c.x - 4.5f, c.y - 4.5f, 9.0f, 9.0f);
}

//==============================================================================
FxPage::FxPage (ValvedProcessor& p) : eqUnit (p), delayUnit (p, FxUnit::DelayUnit), reverbUnit (p, FxUnit::ReverbUnit)
{
    addAndMakeVisible (eqUnit);
    addAndMakeVisible (delayUnit);
    addAndMakeVisible (reverbUnit);
}

void FxPage::refresh() { eqUnit.refresh(); delayUnit.refresh(); reverbUnit.refresh(); }

void FxPage::paint (juce::Graphics& g)
{
    g.setColour (textDim);
    g.setFont (Fonts::condensed (16.0f));
    g.drawText ("AMP  >  CAB  >  EQ  >  DELAY  >  REVERB  >  OUTPUT", getLocalBounds().removeFromTop (30).translated (0, 8), juce::Justification::centred);
}

void FxPage::resized()
{
    auto r = getLocalBounds().reduced (16, 0).withTrimmedTop (42).withTrimmedBottom (14);
    eqUnit.setBounds (r.removeFromLeft (240));
    r.removeFromLeft (14);
    const int w = (r.getWidth() - 14) / 2;
    delayUnit.setBounds (r.removeFromLeft (w));
    r.removeFromLeft (14);
    reverbUnit.setBounds (r);
}

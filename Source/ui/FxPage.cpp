#include "FxPage.h"
#include "../PluginProcessor.h"

using namespace ui;

// Efeitos depois do amp (UI kit "POST FX"): as fotos do EQ, do delay e do reverb ficam como estão,
// e os controles vão por cima. Coordenadas em px das fotos originais (821 x 1916).
namespace
{
    constexpr float knobStart = juce::MathConstants<float>::pi * (7.0f / 6.0f);
    constexpr float knobEnd   = juce::MathConstants<float>::pi * (17.0f / 6.0f);

    // Delay
    constexpr float dlyBox[4] = { 9, 205, 803, 1518 };
    const float dlyKnobX[6] = { 187.5f, 414.5f, 642.0f, 187.5f, 414.5f, 642.0f };
    const float dlyKnobY[6] = { 845.0f, 845.0f, 845.0f, 1090.0f, 1090.0f, 1090.0f };
    // Reverb
    constexpr float revBox[4] = { 12, 177, 801, 1555 };
    const float revKnobX[6] = { 181.0f, 414.0f, 648.5f, 181.0f, 414.0f, 648.5f };
    const float revKnobY[6] = { 847.5f, 847.5f, 847.5f, 1112.0f, 1112.0f, 1112.0f };

    void drawText (juce::Graphics& g, const imgui::Mapper& m, const juce::String& t, juce::Font f, float x, float y, float w, float h,
                   juce::Justification j, juce::Colour c)
    {
        g.setColour (c);
        g.setFont (f.withHeight (f.getHeight() * m.sc()));
        g.drawFittedText (t, m.rect (x, y, w, h).toNearestInt(), j, 1, 0.8f);
    }
}

//==============================================================================
void LeverSwitch::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    static const juce::Image img = imgui::load (BinaryData::dly_sync_png, BinaryData::dly_sync_pngSize);
    const auto r = getLocalBounds().toFloat();
    if (getToggleState()) imgui::draw (g, img, r);
    else
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.addTransform (juce::AffineTransform::scale (1.0f, -1.0f, 0.0f, r.getCentreY()));
        imgui::draw (g, img, r);
    }
    if (highlighted) { g.setColour (juce::Colours::white.withAlpha (0.06f)); g.fillEllipse (r.reduced (r.getWidth() * 0.1f)); }
}

//==============================================================================
FxUnit::FxUnit (ValvedProcessor& p, Kind k) : proc (p), kind (k)
{
    auto& s = p.apvts;
    using namespace BinaryData;
    accentCol = kind == DelayUnit ? juce::Colour (0xffffb23a) : juce::Colour (0xff53d6ff);

    if (kind == DelayUnit)
    {
        body = imgui::load (dly_body_png, dly_body_pngSize);
        listLed = imgui::load (dly_listled_on_png, dly_listled_on_pngSize);
        ledOn = imgui::load (dly_led_on_on_png, dly_led_on_on_pngSize);
        ledOff = imgui::load (dly_led_on_off_png, dly_led_on_off_pngSize);
        syncOn = imgui::load (dly_led_sync_on_png, dly_led_sync_on_pngSize);
        syncOff = imgui::load (dly_led_sync_off_png, dly_led_sync_off_pngSize);
        tapOn = imgui::load (dly_led_tap_on_png, dly_led_tap_on_pngSize);
        onSwitch.overlay = tapSwitch.overlay = imgui::load (dly_foot_png, dly_foot_pngSize);
        ids = { ParamID::dlyTime, ParamID::dlyRepeats, ParamID::dlyMix, ParamID::dlyTone, ParamID::dlyMod, ParamID::dlyParam };
        onAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, ParamID::dlyOn, onSwitch);
        addAndMakeVisible (tapSwitch);
        tapSwitch.setTooltip ("Tap tempo");
        tapSwitch.onClick = [this] { tap(); };
        addAndMakeVisible (syncSwitch);
        syncSwitch.setTooltip ("Sync the delay time to the host tempo");
        syncAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, ParamID::dlySync, syncSwitch);
        division.addItemList (ValvedProcessor::divisionNames(), 1);
        division.setAlpha (0.0f);   // o texto é desenhado sobre a caixa da foto; o clique abre a lista
        addAndMakeVisible (division);
        divAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, ParamID::dlyDiv, division);
    }
    else
    {
        body = imgui::load (rev_body_png, rev_body_pngSize);
        listLed = imgui::load (rev_listled_on_png, rev_listled_on_pngSize);
        ledOn = imgui::load (rev_led_on_png, rev_led_on_pngSize);
        ledOff = imgui::load (rev_led_off_png, rev_led_off_pngSize);
        onSwitch.overlay = imgui::load (rev_foot_png, rev_foot_pngSize);
        ids = { ParamID::revDecay, ParamID::revPre, ParamID::revMix, ParamID::revTone, ParamID::revMod, ParamID::revParam };
        onAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, ParamID::revOn, onSwitch);
    }
    addAndMakeVisible (onSwitch);
    onSwitch.setTooltip ("On / Off");

    for (auto& id : ids)
    {
        auto* kn = knobs.add (new juce::Slider());
        kn->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        kn->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        kn->setRotaryParameters (knobStart, knobEnd, true);
        kn->setPopupDisplayEnabled (true, true, nullptr);
        kn->setMouseDragSensitivity (220);
        kn->getProperties().set ("knobStyle", (int) KnobSprite);
        kn->getProperties().set ("sprite", kind == DelayUnit ? 7 : 8);
        addAndMakeVisible (kn);
        knobAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, *kn));
        if (auto* prm = s.getParameter (id))
            kn->setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    }
}

float FxUnit::nativeWidth() const  { return kind == DelayUnit ? dlyBox[2] : revBox[2]; }
float FxUnit::nativeHeight() const { return kind == DelayUnit ? dlyBox[3] : revBox[3]; }

imgui::Mapper FxUnit::mapper() const
{
    const float* b = kind == DelayUnit ? dlyBox : revBox;
    return { getLocalBounds().toFloat(), b[0], b[1], b[2], b[3] };
}

juce::Rectangle<float> FxUnit::itemRect (int t) const
{
    if (kind == DelayUnit)
    {
        // DIGITAL, TAPE, ANALOG, DUAL, PING-PONG | MULTI-TAP, REVERSE, SWELL, DUCKING, LO-FI
        const int col = t / 5, row = t % 5;
        return { (col == 0 ? 470.0f : 627.0f) - 16.0f, 478.0f + 44.0f * (float) row - 22.0f, col == 0 ? 152.0f : 168.0f, 44.0f };
    }
    // ROOM, HALL, PLATE, SPRING, SHIMMER | SHIMMER 5TH (embaixo de GATED), BLOOM, CLOUD, SWELL, GATED
    int col = 0, row = t;
    if (t == 5) { col = 1; row = 4; }
    else if (t > 5) { col = 1; row = t - 6; }
    return { (col == 0 ? 500.0f : 663.0f) - 16.0f, 442.0f + 48.75f * (float) row - 24.0f, col == 0 ? 160.0f : 150.0f, 48.0f };
}

void FxUnit::resized()
{
    const auto m = mapper();
    const float kb = kind == DelayUnit ? 130.0f : 140.0f;
    for (int i = 0; i < knobs.size(); ++i)
        knobs[i]->setBounds (m.box (kind == DelayUnit ? dlyKnobX[i] : revKnobX[i], kind == DelayUnit ? dlyKnobY[i] : revKnobY[i], kb, kb).toNearestInt());
    if (kind == DelayUnit)
    {
        onSwitch.setBounds (m.box (498.0f, 1462.0f, 160.0f, 160.0f).toNearestInt());
        tapSwitch.setBounds (m.box (682.0f, 1462.0f, 160.0f, 160.0f).toNearestInt());
        syncSwitch.setBounds (m.rect (73.0f, 1440.0f, 67.0f, 70.0f).toNearestInt());
        division.setBounds (m.rect (195.0f, 1440.0f, 183.0f, 65.0f).toNearestInt());
    }
    else
        onSwitch.setBounds (m.box (566.0f, 1490.0f, 192.0f, 192.0f).toNearestInt());
}

void FxUnit::mouseDown (const juce::MouseEvent& e)
{
    const auto m = mapper();
    const int n = kind == DelayUnit ? vlv::StereoDelay::typeNames().size() : vlv::AmbientReverb::typeNames().size();
    for (int i = 0; i < n; ++i)
    {
        const auto r = itemRect (i);
        if (m.rect (r.getX(), r.getY(), r.getWidth(), r.getHeight()).contains (e.position))
        {
            if (auto* prm = proc.apvts.getParameter (kind == DelayUnit ? ParamID::dlyType : ParamID::revType))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i));
                prm->endChangeGesture();
            }
            refresh();
            return;
        }
    }
}

void FxUnit::paint (juce::Graphics& g)
{
    const auto m = mapper();
    {
        const auto r = getLocalBounds().toFloat().reduced ((float) getWidth() * 0.03f, (float) getHeight() * 0.02f);
        juce::Path p; p.addRoundedRectangle (r, r.getWidth() * 0.06f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.8f), 24, { 0, 12 }).drawForPath (g, p);
    }
    imgui::draw (g, body, getLocalBounds().toFloat());

    const auto ink = onState ? accentCol : accentCol.withAlpha (0.4f);
    if (kind == DelayUnit)
    {
        // display
        drawText (g, m, shown1, Fonts::wide (34.0f), 97.0f, 500.0f, 300.0f, 42.0f, juce::Justification::centredLeft, ink);
        drawText (g, m, shown2, Fonts::condensedMedium (27.0f), 104.0f, 634.0f, 300.0f, 34.0f, juce::Justification::centredLeft, ink.withAlpha (onState ? 0.9f : 0.35f));
        if (! onState) drawText (g, m, "BYPASS", Fonts::condensedMedium (24.0f), 318.0f, 478.0f, 76.0f, 26.0f, juce::Justification::centredRight, accentCol.withAlpha (0.8f));
        // divisão (sobre a caixa da foto)
        drawText (g, m, divText, Fonts::condensedMedium (28.0f), 214.0f, 1450.0f, 110.0f, 46.0f, juce::Justification::centredLeft,
                  juce::Colour (0xffffd27a).withAlpha (syncState ? 0.95f : 0.35f));
        // LEDs
        imgui::draw (g, onState ? ledOn : ledOff, m.box (498.0f, 1357.0f, 44.0f, 44.0f));
        imgui::draw (g, syncState ? syncOn : syncOff, m.box (105.0f, 1400.0f, 44.0f, 44.0f));
        if (tapLed) imgui::draw (g, tapOn, m.box (682.0f, 1357.0f, 44.0f, 44.0f));
    }
    else
    {
        drawText (g, m, shown1, Fonts::wide (36.0f), 98.0f, 450.0f, 236.0f, 44.0f, juce::Justification::centredLeft, ink);
        drawText (g, m, shown2, Fonts::condensedMedium (25.0f), 88.0f, 606.0f, 336.0f, 34.0f, juce::Justification::centredLeft, ink.withAlpha (onState ? 0.9f : 0.35f));
        drawText (g, m, onState ? "ON" : "BYPASS", Fonts::condensedBold (22.0f), 340.0f, 435.0f, 78.0f, 33.0f, juce::Justification::centred,
                  onState ? accentCol.withAlpha (0.9f) : juce::Colours::white.withAlpha (0.6f));
        // o SHIMMER 5TH não está na foto: entra embaixo de GATED, no mesmo estilo
        const auto r = itemRect (5);
        const auto c = m.pt (663.0f, r.getCentreY());
        const float lr = 7.5f * m.sc();
        g.setColour (juce::Colour (0xff0d1416)); g.fillEllipse (c.x - lr * 1.35f, c.y - lr * 1.35f, lr * 2.7f, lr * 2.7f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3e5258), c.x - lr * 0.4f, c.y - lr * 0.5f, juce::Colour (0xff10191b), c.x + lr, c.y + lr, true));
        g.fillEllipse (c.x - lr, c.y - lr, lr * 2.0f, lr * 2.0f);
        drawText (g, m, "SHIMMER 5TH", Fonts::condensedBold (25.0f), 688.0f, r.getY() + 8.0f, 112.0f, 32.0f, juce::Justification::centredLeft,
                  juce::Colour (0xffc5ced3).withAlpha (selected == 5 ? 1.0f : 0.8f));
        imgui::draw (g, onState ? ledOn : ledOff, m.box (575.0f, 1367.0f, 44.0f, 44.0f));
    }
    // LED do tipo escolhido
    if (selected >= 0)
    {
        const auto r = itemRect (selected);
        imgui::draw (g, listLed, m.box (r.getX() + 16.0f, r.getCentreY(), kind == DelayUnit ? 32.0f : 34.0f, kind == DelayUnit ? 32.0f : 34.0f));
    }
    // rótulo do 6º knob (muda conforme o tipo)
    drawText (g, m, specialLabel, Fonts::condensedBold (29.0f), kind == DelayUnit ? 574.0f : 578.0f, kind == DelayUnit ? 1196.0f : 1208.0f,
              140.0f, 32.0f, juce::Justification::centred, accentCol);
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
            return juce::String (ms, 0) + " ms @ " + juce::String (proc.getBpm(), 0) + "   " + mix;
        return juce::String (ms, 0) + " ms     " + mix;
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
    const bool on = s.getRawParameterValue (kind == DelayUnit ? ParamID::dlyOn : ParamID::revOn)->load() > 0.5f;
    const juce::String l1 = displayLine1(), l2 = displayLine2();
    const juce::String sp = juce::String (kind == DelayUnit ? vlv::StereoDelay::paramName (type) : vlv::AmbientReverb::paramName (type)).toUpperCase();

    bool tl = false, sync = false;
    juce::String dv;
    if (kind == DelayUnit)
    {
        sync = s.getRawParameterValue (ParamID::dlySync)->load() > 0.5f;
        knobs[0]->setEnabled (! sync);
        knobs[0]->setAlpha (sync ? 0.5f : 1.0f);
        division.setEnabled (sync);
        dv = ValvedProcessor::divisionNames()[(int) s.getRawParameterValue (ParamID::dlyDiv)->load()];
        const double period = juce::jmax (40.0, (double) proc.getEffectiveDelayMs());
        tl = on && std::fmod (juce::Time::getMillisecondCounterHiRes(), period) < juce::jmin (90.0, period * 0.4);
    }

    if (l1 != shown1 || l2 != shown2 || sp != specialLabel || on != onState || tl != tapLed || type != selected || sync != syncState || dv != divText)
    {
        shown1 = l1; shown2 = l2; specialLabel = sp; onState = on; tapLed = tl; selected = type; syncState = sync; divText = dv;
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
    static const juce::Image cap = imgui::load (BinaryData::eq_cap_png, BinaryData::eq_cap_pngSize);
    const auto b = getLocalBounds().toFloat();
    const float y0 = (float) getPositionOfValue (0.0), yv = (float) getPositionOfValue (getValue());
    const float cx = b.getCentreX();
    // filete aceso do 0 até o valor (como na foto do UI kit)
    g.setColour (juce::Colour (0xff6fe3e0).withAlpha (isEnabled() ? 0.8f : 0.3f));
    g.fillRect (cx - capW * 0.035f, juce::jmin (y0, yv), capW * 0.07f, std::abs (yv - y0));
    auto r = juce::Rectangle<float> (capW, capH).withCentre ({ cx, yv });
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (r.translated (0.0f, capH * 0.18f).expanded (capW * 0.04f), capH * 0.35f);
    imgui::draw (g, cap, r);
}

EqUnit::EqUnit (ValvedProcessor& p) : proc (p)
{
    using namespace BinaryData;
    body = imgui::load (eq_body_png, eq_body_pngSize);
    bodyMark = imgui::load (eq_body_mark_png, eq_body_mark_pngSize);   // MARK: só 5 trilhos, mais espaçados
    btnOn = imgui::load (eq_btn_on_png, eq_btn_on_pngSize);
    btnOff = imgui::load (eq_btn_off_png, eq_btn_off_pngSize);
    ledOff = imgui::load (eq_ledoff_png, eq_ledoff_pngSize);
    onSwitch.overlay = imgui::load (eq_foot_png, eq_foot_pngSize);

    auto& s = p.apvts;
    auto add = [&] (juce::OwnedArray<EqFader>& arr, const juce::String& id)
    {
        auto* f = arr.add (new EqFader());
        addChildComponent (f);
        att.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, *f));
        f->setDoubleClickReturnValue (true, 0.0);
    };
    for (int b = 0; b < 7; ++b) add (vFaders, ParamID::eqBand (0, b));
    for (int b = 0; b < 5; ++b) add (mFaders, ParamID::eqBand (1, b));
    addAndMakeVisible (level);
    att.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, ParamID::eqLevel, level));
    level.setDoubleClickReturnValue (true, 0.0);

    addAndMakeVisible (modeV);
    addAndMakeVisible (modeM);
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
    onSwitch.setTooltip ("On / Off");
    onAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, ParamID::eqOn, onSwitch);
    refresh();
}

imgui::Mapper EqUnit::mapper() const { return { getLocalBounds().toFloat(), 14.0f, 128.0f, 793.0f, 1659.0f }; }

void EqUnit::refresh()
{
    const int m = (int) proc.apvts.getRawParameterValue (ParamID::eqMode)->load();
    const bool on = proc.apvts.getRawParameterValue (ParamID::eqOn)->load() > 0.5f;
    if (m != mode)
    {
        mode = m;
        for (auto* f : vFaders) f->setVisible (mode == 0);
        for (auto* f : mFaders) f->setVisible (mode == 1);
        repaint();
    }
    if (on != onState) { onState = on; repaint(); }
}

void EqUnit::resized()
{
    const auto m = mapper();
    modeV.setBounds (m.rect (90.0f, 415.0f, 320.0f, 85.0f).toNearestInt());
    modeM.setBounds (m.rect (432.0f, 415.0f, 318.0f, 85.0f).toNearestInt());
    // trilhos da foto: +12 dB em y 678, -12 dB em y 1127
    static const float trackX[8] = { 172.0f, 240.0f, 310.0f, 380.0f, 450.0f, 520.0f, 590.0f, 695.0f };
    const float sc = m.sc();
    const float w = 64.0f * sc;
    const float pad = juce::jmin (12.0f, w * 0.5f);   // margem interna do slider vertical (raio da "bolinha" do LookAndFeel)
    auto place = [&] (EqFader& f, float x)
    {
        const auto top = m.pt (x, 678.0f), bot = m.pt (x, 1127.0f);
        f.setBounds (juce::Rectangle<float> (top.x - w * 0.5f, top.y - pad, w, bot.y - top.y + 2.0f * pad).toNearestInt());
        f.capW = 58.0f * sc; f.capH = 33.0f * sc;
    };
    for (int i = 0; i < vFaders.size(); ++i) place (*vFaders[i], trackX[i]);
    for (int i = 0; i < mFaders.size(); ++i) place (*mFaders[i], 172.0f + 104.5f * (float) i);
    place (level, trackX[7]);
    onSwitch.setBounds (m.box (410.0f, 1492.0f, 256.0f, 256.0f).toNearestInt());
}

void EqUnit::paint (juce::Graphics& g)
{
    const auto m = mapper();
    {
        const auto r = getLocalBounds().toFloat().reduced ((float) getWidth() * 0.03f, (float) getHeight() * 0.02f);
        juce::Path p; p.addRoundedRectangle (r, r.getWidth() * 0.06f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.8f), 24, { 0, 12 }).drawForPath (g, p);
    }
    imgui::draw (g, mode == 1 ? bodyMark : body, getLocalBounds().toFloat());
    // botões VALVED / MARK (o escolhido acende em laranja)
    const auto rv = m.rect (90.0f, 415.0f, 320.0f, 85.0f), rm = m.rect (432.0f, 415.0f, 318.0f, 85.0f);
    imgui::draw (g, mode == 0 ? btnOn : btnOff, rv);
    imgui::draw (g, mode == 1 ? btnOn : btnOff, rm);
    auto label = [&] (juce::Rectangle<float> r, const char* t, bool lit)
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.addTransform (juce::AffineTransform::shear (-0.18f, 0.0f).translated (r.getCentreY() * 0.18f, 0.0f));
        g.setFont (Fonts::condensedBold (36.0f * m.sc()));
        g.setColour (lit ? juce::Colour (0xfffff3c8) : juce::Colour (0xffc9ccd1).withAlpha (0.85f));
        g.drawText (t, r, juce::Justification::centred);
    };
    label (rv, "VALVED", mode == 0);
    label (rm, "MARK", mode == 1);
    // rótulos das bandas
    static const char* vl[8] = { "80", "200", "400", "800", "1k6", "3k2", "6k4", "LEVEL" };
    static const char* ml[8] = { "80", "240", "750", "2200", "6600", "", "", "LEVEL" };
    static const float trackX[8] = { 172.0f, 240.0f, 310.0f, 380.0f, 450.0f, 520.0f, 590.0f, 695.0f };
    for (int i = 0; i < 8; ++i)
    {
        const float x = (mode == 1 && i < 5) ? 172.0f + 104.5f * (float) i : trackX[i];
        drawText (g, m, mode == 1 ? ml[i] : vl[i], Fonts::condensedBold (26.0f), x - 40.0f, 1190.0f, 80.0f, 32.0f,
                  juce::Justification::centred, juce::Colour (0xffd5d8dc).withAlpha (i == 7 ? 0.95f : 0.8f));
    }
    if (! onState)
        imgui::draw (g, ledOff, m.box (414.0f, 1323.0f, 52.0f, 52.0f));
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
    auto r = getLocalBounds().toFloat().reduced (16.0f, 0.0f).withTrimmedTop (36.0f).withTrimmedBottom (8.0f);
    const float gap = 26.0f;
    const float sumW = eqUnit.nativeWidth() + delayUnit.nativeWidth() + reverbUnit.nativeWidth();
    const float maxH = juce::jmax (eqUnit.nativeHeight(), delayUnit.nativeHeight(), reverbUnit.nativeHeight());
    const float sc = juce::jmin ((r.getWidth() - 2.0f * gap) / sumW, r.getHeight() / maxH);
    float x = r.getCentreX() - (sumW * sc + 2.0f * gap) * 0.5f;
    auto place = [&] (juce::Component& c, float nw, float nh)
    {
        c.setBounds (juce::Rectangle<float> (x, r.getY() + (maxH - nh) * sc * 0.5f, nw * sc, nh * sc).toNearestInt());
        x += nw * sc + gap;
    };
    place (eqUnit, eqUnit.nativeWidth(), eqUnit.nativeHeight());
    place (delayUnit, delayUnit.nativeWidth(), delayUnit.nativeHeight());
    place (reverbUnit, reverbUnit.nativeWidth(), reverbUnit.nativeHeight());
}

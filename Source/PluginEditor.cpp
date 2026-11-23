#include "PluginEditor.h"

using namespace ui;

//==============================================================================
TopKnob::TopKnob (juce::AudioProcessorValueTreeState& s, const char* paramId, const juce::String& name)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 80, 18);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (220);
    slider.getProperties().set ("knobStyle", (int) KnobModern);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxTextColourId, textDim.brighter (0.4f));
    addAndMakeVisible (slider);
    label.setText (name, juce::dontSendNotification);
    label.setFont (Fonts::condensedBold (15.0f));
    label.setColour (juce::Label::textColourId, text);
    label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (label);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, paramId, slider);
}

void TopKnob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (18));
    slider.setBounds (r);
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff050506));
    g.fillRoundedRectangle (r, 2.0f);
    const float db = juce::Decibels::gainToDecibels (level, -60.0f);
    const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    auto fill = r.reduced (2.0f);
    fill = fill.removeFromBottom (fill.getHeight() * frac);
    g.setColour (level >= 0.99f ? juce::Colour (0xffff3b30) : (db > -6.0f ? accent : juce::Colour (0xff4cd37a)));
    g.fillRoundedRectangle (fill, 1.5f);
}

//==============================================================================
ValvedEditor::ValvedEditor (ValvedProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      input (p.apvts, ParamID::input, "INPUT"),
      gate (p.apvts, ParamID::gate, "GATE"),
      transpose (p.apvts, ParamID::transpose, "TRANSPOSE"),
      output (p.apvts, ParamID::output, "OUTPUT"),
      presetBar (p),
      head (p.apvts),
      cabPage (p), pedalsPage (p), fxPage (p), tunerView (p)
{
    setLookAndFeel (&look);

    for (auto* t : { &pedalsTab, &ampTab, &cabTab, &fxTab })
    {
        t->getProperties().set ("tab", true);
        addAndMakeVisible (t);
    }
    pedalsTab.onClick = [this] { showPage (PagePedals); };
    ampTab.onClick    = [this] { showPage (PageAmp); };
    cabTab.onClick    = [this] { showPage (PageCab); };
    fxTab.onClick     = [this] { showPage (PageFx); };

    for (auto* k : { &input, &gate, &transpose, &output })
        addAndMakeVisible (k);

    tuningLabel.setFont (Fonts::condensed (15.0f));
    tuningLabel.setColour (juce::Label::textColourId, accent);
    tuningLabel.setJustificationType (juce::Justification::centredLeft);
    addChildComponent (tuningLabel);   // (texto da afinação removido do cabeçalho: o próprio knob TRANSPOSE já mostra os semitons)
    addAndMakeVisible (presetBar);

    for (auto* f : { &flagEn, &flagPt })
    {
        addAndMakeVisible (f);
        f->setTooltip (f->language == lang::English ? "English" : "Portuguese (Brazil)");
        f->onClick = [this, f] { lang::set (f->language); refreshLanguage(); };
    }

    tunerBtn.setClickingTogglesState (true);
    tunerBtn.setTooltip ("Tuner");
    tunerBtn.onClick = [this]
    {
        proc.tunerActive = tunerBtn.getToggleState();
        tunerView.setVisible (tunerBtn.getToggleState());
        if (tunerBtn.getToggleState()) tunerView.toFront (false);
    };
    addAndMakeVisible (tunerBtn);

    camBtn.setClickingTogglesState (true);
    camBtn.getProperties().set ("noTr", true);
    camBtn.setToggleState (proc.camLink.isEnabled(), juce::dontSendNotification);
    camBtn.onClick = [this] { proc.camLink.setEnabled (camBtn.getToggleState()); lastCamStatus = -1; timerCallback(); };
    addAndMakeVisible (camBtn);

    // Qualidade (sobreamostragem)
    const char* qTips[3] = { "Low: lighter on the CPU (for simpler computers)",
                             "Medium: balanced quality and CPU",
                             "High: best possible quality (uses much more CPU)" };
    for (int q = 0; q < 3; ++q)
    {
        auto& b = qualityBtn[q];
        b.setClickingTogglesState (false);
        b.setTooltip (qTips[q]);
        b.setConnectedEdges ((q > 0 ? juce::Button::ConnectedOnLeft : 0) | (q < 2 ? juce::Button::ConnectedOnRight : 0));
        b.onClick = [this, q]
        {
            if (auto* prm = proc.apvts.getParameter (ParamID::quality))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) q));
                prm->endChangeGesture();
            }
            for (int k = 0; k < 3; ++k) qualityBtn[k].setToggleState (k == q, juce::dontSendNotification);
        };
        addAndMakeVisible (b);
    }

    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

    const auto& names = vlv::ampNames();
    for (int i = 0; i < (int) names.size(); ++i)
    {
        auto* b = ampButtons.add (new juce::TextButton (juce::String (names[(size_t) i]).toUpperCase()));
        b->onClick = [this, i]
        {
            if (auto* prm = proc.apvts.getParameter (ParamID::amp))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i));
                prm->endChangeGesture();
            }
        };
        addAndMakeVisible (b);
    }

    addAndMakeVisible (head);
    head.toBack();   // os botões de amp/motor ficam por cima da foto do amp
    addChildComponent (cabPage);
    addChildComponent (pedalsPage);
    addChildComponent (fxPage);
    addChildComponent (tunerView);
    tunerBtn.setToggleState (proc.tunerActive.load(), juce::dontSendNotification);
    tunerView.setVisible (proc.tunerActive.load());

    if (! proc.license.isActive())
    {
        juce::Component::SafePointer<ValvedEditor> self (this);
        activation = std::make_unique<ActivationView> (proc.license, [self]
        {
            juce::MessageManager::callAsync ([self]
            {
                if (auto* ed = self.getComponent()) { ed->activation.reset(); ed->repaint(); }
            });
        });
        addAndMakeVisible (*activation);
    }

    setSize (1100, 720);
    showPage (PageAmp);
    startTimerHz (30);
    refreshLanguage();
    timerCallback();
}

ValvedEditor::~ValvedEditor()
{
    stopTimer();
    proc.tunerActive = false;   // fechou a janela: o afinador desliga (e o som volta)
    setLookAndFeel (nullptr);
}

void ValvedEditor::showPage (int newPage)
{
    page = newPage;
    pedalsTab.setToggleState (page == PagePedals, juce::dontSendNotification);
    ampTab.setToggleState (page == PageAmp, juce::dontSendNotification);
    cabTab.setToggleState (page == PageCab, juce::dontSendNotification);
    fxTab.setToggleState (page == PageFx, juce::dontSendNotification);
    head.setVisible (page == PageAmp);
    for (auto* b : ampButtons) b->setVisible (page == PageAmp);
    cabPage.setVisible (page == PageCab);
    pedalsPage.setVisible (page == PagePedals);
    fxPage.setVisible (page == PageFx);
    if (tunerView.isVisible()) tunerView.toFront (false);
    repaint();
}

juce::String ValvedEditor::tuningName (int semis)
{
    static const char* notes[] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    if (semis == 0) return lang::t ("E Standard (no shift)", "Afinação padrão em E");
    const int idx = ((4 + semis) % 12 + 12) % 12;
    const int a = std::abs (semis);
    if (lang::pt())
    {
        const juce::String amount = a == 12 ? juce::String ("1 oitava") : juce::String (a) + (a == 1 ? " semitom" : " semitons");
        return juce::String (juce::CharPointer_UTF8 ("Afinação em ")) + notes[idx] + "  (" + amount + (semis < 0 ? " abaixo)" : " acima)");
    }
    juce::String amount = a == 12 ? juce::String ("1 octave")
                                  : juce::String (a) + (a == 1 ? " semitone" : " semitones");
    return juce::String (notes[idx]) + " Standard  (" + amount + (semis < 0 ? " down)" : " up)");
}

void ValvedEditor::FlagButton::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const bool sel = lang::current() == language;
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    juce::Graphics::ScopedSaveState ss (g);
    juce::Path clip; clip.addRoundedRectangle (r, 2.5f);
    g.reduceClipRegion (clip);
    if (language == lang::Portuguese)
    {
        g.setColour (juce::Colour (0xff009c3b)); g.fillRect (r);
        juce::Path d; const auto c = r.getCentre();
        d.addQuadrilateral (c.x, r.getY() + r.getHeight() * 0.12f, r.getRight() - r.getWidth() * 0.09f, c.y,
                            c.x, r.getBottom() - r.getHeight() * 0.12f, r.getX() + r.getWidth() * 0.09f, c.y);
        g.setColour (juce::Colour (0xffffdf00)); g.fillPath (d);
        const float cr = r.getHeight() * 0.27f;
        g.setColour (juce::Colour (0xff002776)); g.fillEllipse (c.x - cr, c.y - cr, 2 * cr, 2 * cr);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        juce::Path band; band.addCentredArc (c.x + cr * 0.2f, c.y + cr * 1.6f, cr * 1.9f, cr * 1.9f, 0.0f, -0.62f, 0.5f, true);
        g.strokePath (band, juce::PathStrokeType (r.getHeight() * 0.06f), {});
    }
    else
    {
        const float sh = r.getHeight() / 13.0f;
        for (int i = 0; i < 13; ++i)
        {
            g.setColour (i % 2 == 0 ? juce::Colour (0xffb22234) : juce::Colours::white);
            g.fillRect (r.getX(), r.getY() + sh * (float) i, r.getWidth(), sh + 0.5f);
        }
        const auto canton = juce::Rectangle<float> (r.getX(), r.getY(), r.getWidth() * 0.42f, sh * 7.0f);
        g.setColour (juce::Colour (0xff3c3b6e)); g.fillRect (canton);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        for (int y = 0; y < 3; ++y) for (int x = 0; x < 4; ++x)
            g.fillEllipse (canton.getX() + canton.getWidth() * (0.14f + 0.24f * (float) x) - 0.6f, canton.getY() + canton.getHeight() * (0.2f + 0.3f * (float) y) - 0.6f, 1.2f, 1.2f);
    }
    if (! sel) { g.setColour (juce::Colours::black.withAlpha (highlighted ? 0.35f : 0.55f)); g.fillRect (r); }
}

void ValvedEditor::refreshLanguage()
{
    input.label.setText (lang::t ("INPUT", "ENTRADA"), juce::dontSendNotification);
    gate.label.setText ("GATE", juce::dontSendNotification);
    transpose.label.setText (lang::t ("TRANSPOSE", "TRANSPOR"), juce::dontSendNotification);
    output.label.setText (lang::t ("OUTPUT", "SAÍDA"), juce::dontSendNotification);
    const char* qEn[3] = { "LOW", "MED", "HIGH" };
    const char* qPt[3] = { "BAIXA", "MÉDIA", "ALTA" };
    for (int q = 0; q < 3; ++q)
    {
        qualityBtn[q].getProperties().set ("noTr", true);
        qualityBtn[q].setButtonText (lang::t (qEn[q], qPt[q]));
    }
    lang::applyTree (*this);
    if (activation != nullptr) activation->refreshLanguage();
    timerCallback();
    repaint();
    for (auto* c : getChildren()) c->repaint();
}

// Amps com foto (REC): a imagem ocupa a área toda e os botões ficam por cima dela.
// Amps desenhados: ficam abaixo da fileira de botões, como antes.
void ValvedEditor::layoutHead()
{
    if (head.isImageAmp()) head.setBounds (mainArea);
    else head.setBounds (mainArea.withTrimmedTop (56).reduced (10, 4));
    for (auto* b : ampButtons) b->toFront (false);
}

void ValvedEditor::timerCallback()
{
    {
        // botão CAM: desligado / procurando o celular / conectado
        const int st = (int) proc.camLink.getStatus();
        if (st != lastCamStatus)
        {
            lastCamStatus = st;
            camBtn.setButtonText (st == vlv::CamLink::Connected ? juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f CAM"))
                                : st == vlv::CamLink::Searching ? juce::String ("CAM ...") : juce::String ("CAM"));
            const auto camCol = st == vlv::CamLink::Connected ? juce::Colour (0xff59c97a) : st == vlv::CamLink::Searching ? juce::Colour (0xfff0a045) : ui::textDim;
            camBtn.setColour (juce::TextButton::textColourOnId, camCol);
            camBtn.setColour (juce::TextButton::textColourOffId, camCol);
            camBtn.setToggleState (st != vlv::CamLink::Off, juce::dontSendNotification);
            camBtn.setTooltip (st == vlv::CamLink::Connected
                ? lang::t ("Valved Cam connected: ", "Valved Cam conectado: ") + proc.camLink.getDeviceName()
                : st == vlv::CamLink::Searching
                    ? lang::t ("Looking for the Valved Cam app (same Wi-Fi or USB tethering)...", "Procurando o app Valved Cam (mesmo Wi-Fi ou ancoragem USB)...")
                    : lang::t ("Valved Cam: record phone video with the Valved sound", "Valved Cam: grave o vídeo do celular com o som do Valved"));
        }
    }

    const int ampIdx = (int) proc.apvts.getRawParameterValue (ParamID::amp)->load();
    for (int i = 0; i < ampButtons.size(); ++i)
        ampButtons[i]->setToggleState (i == ampIdx, juce::dontSendNotification);
    {
        const bool dz = ampIdx == 3 && proc.apvts.getRawParameterValue (ParamID::dzMod)->load() > 0.5f;
        const int hm = dz ? 4 : ampIdx;                          // REC com DZ MOD ligado mostra os 8 knobs
        if (hm != head.getModel()) { head.setModel (hm); layoutHead(); }
    }
    head.refreshState();

    const int semis = (int) std::lround (proc.apvts.getRawParameterValue (ParamID::transpose)->load());
    juce::String tn = tuningName (semis);
    if (semis != 0 && proc.getSampleRate() > 0 && proc.getLatencySamples() > 0)
        tn += "\n" + juce::String (1000.0 * proc.getLatencySamples() / proc.getSampleRate(), 0) + lang::t (" ms latency", " ms de latência");
    tuningLabel.setText (tn, juce::dontSendNotification);

    if (page == PageCab) cabPage.refresh();
    if (page == PagePedals) pedalsPage.refresh();
    if (page == PageFx) fxPage.refresh();
    if (tunerView.isVisible()) tunerView.tick();
    presetBar.tick();
    {
        const int q = (int) proc.apvts.getRawParameterValue (ParamID::quality)->load();
        for (int k = 0; k < 3; ++k)
            if (qualityBtn[k].getToggleState() != (k == q)) qualityBtn[k].setToggleState (k == q, juce::dontSendNotification);
    }
    inMeter.setLevel (proc.getInputLevel());
    outMeter.setLevel (proc.getOutputLevel());
}

void ValvedEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    // Barra superior
    g.setColour (bar);
    g.fillRect (topBar);
    g.setColour (line);
    g.fillRect (topBar.getX(), topBar.getBottom() - 1, topBar.getWidth(), 1);
    g.fillRect (topBar.getX(), 58, topBar.getWidth(), 1);

    {
        // logo (imagem): metal escovado com o raio embaixo
        static const juce::Image logo = juce::ImageCache::getFromMemory (BinaryData::valved_logo_png, BinaryData::valved_logo_pngSize);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (logo, logoArea.toFloat(), juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid | juce::RectanglePlacement::onlyReduceInSize);
    }

    g.setColour (textDim);
    g.setFont (Fonts::condensed (14.0f));
    g.drawText ("v0.47", getLocalBounds().removeFromTop (58).reduced (24, 0), juce::Justification::centredRight);
    g.setFont (Fonts::condensedBold (12.0f));
    g.drawFittedText (lang::t ("QUALITY", "QUALIDADE"), qualityLabelArea, juce::Justification::centredRight, 1, 0.8f);

    g.setFont (Fonts::condensedBold (12.0f));
    g.drawText (lang::t ("IN", "ENT"), inMeter.getBounds().translated (0, inMeter.getHeight() + 2).withHeight (12).expanded (8, 0), juce::Justification::centred);
    g.drawText (lang::t ("OUT", "SAÍ"), outMeter.getBounds().translated (0, outMeter.getHeight() + 2).withHeight (12).expanded (8, 0), juce::Justification::centred);

    // Fundo do palco (leve iluminação no centro)
    juce::ColourGradient stage (juce::Colour (0xff1d1d22), (float) mainArea.getCentreX(), (float) mainArea.getY() + 40.0f,
                                bg, (float) mainArea.getCentreX(), (float) mainArea.getBottom(), true);
    g.setGradientFill (stage);
    g.fillRect (mainArea);
}

void ValvedEditor::resized()
{
    if (activation != nullptr) { activation->setBounds (getLocalBounds()); activation->toFront (false); }
    auto r = getLocalBounds();
    topBar = r.removeFromTop (170);
    mainArea = r;

    auto row1 = juce::Rectangle<int> (0, 0, getWidth(), 58);
    logoArea = juce::Rectangle<int> (30, 17, 128, 28);   // discreto: ~28 px de altura
    flagEn.setBounds (184, 20, 28, 20);
    flagPt.setBounds (216, 20, 28, 20);
    camBtn.setBounds (254, 15, 70, 28);
    auto tabs = row1.withSizeKeepingCentre (440, 58);
    for (auto* t : { &pedalsTab, &ampTab, &cabTab, &fxTab })
        t->setBounds (tabs.removeFromLeft (110));

    {
        // Qualidade: no cabeçalho, à esquerda do número da versão
        auto qa = juce::Rectangle<int> (getWidth() - 24 - 46 - 3 * 46, 15, 3 * 46, 28);
        for (int q = 0; q < 3; ++q) qualityBtn[q].setBounds (qa.getX() + q * 46, qa.getY(), 46, qa.getHeight());
        qualityLabelArea = juce::Rectangle<int> (qa.getX() - 70, qa.getY(), 62, qa.getHeight());
    }
    auto row2 = juce::Rectangle<int> (0, 66, getWidth(), 100).reduced (26, 0);
    input.setBounds (row2.removeFromLeft (92));
    row2.removeFromLeft (10);
    gate.setBounds (row2.removeFromLeft (92));
    row2.removeFromLeft (10);
    transpose.setBounds (row2.removeFromLeft (92));
    tuningLabel.setBounds (0, 0, 0, 0);

    auto meters = row2.removeFromRight (44).withTrimmedTop (14).withTrimmedBottom (18);
    inMeter.setBounds (meters.removeFromLeft (14));
    meters.removeFromLeft (14);
    outMeter.setBounds (meters.removeFromLeft (14));
    row2.removeFromRight (12);
    output.setBounds (row2.removeFromRight (92));
    row2.removeFromRight (24);
    tunerBtn.setBounds (row2.removeFromRight (104).withSizeKeepingCentre (104, 34).translated (0, 4));
    // presets no meio, entre o TRANSPOSE e o afinador
    presetBar.setBounds (row2.withSizeKeepingCentre (280, 56).translated (0, -5));

    // Página AMP
    auto m = mainArea;
    // os 4 amps, centralizados
    auto sel = m.removeFromTop (56).withSizeKeepingCentre (4 * 130 + 3 * 8, 34).translated (0, 6);
    for (auto* b : ampButtons) { b->setBounds (sel.removeFromLeft (130)); sel.removeFromLeft (8); }
    layoutHead();

    // Página CAB
    cabPage.setBounds (mainArea.withTrimmedTop (16));
    pedalsPage.setBounds (mainArea);
    fxPage.setBounds (mainArea);
    tunerView.setBounds (mainArea);
}

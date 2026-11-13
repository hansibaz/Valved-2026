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
    addAndMakeVisible (tuningLabel);

    tunerBtn.setClickingTogglesState (true);
    tunerBtn.setTooltip ("Tuner");
    tunerBtn.onClick = [this]
    {
        proc.tunerActive = tunerBtn.getToggleState();
        tunerView.setVisible (tunerBtn.getToggleState());
        if (tunerBtn.getToggleState()) tunerView.toFront (false);
    };
    addAndMakeVisible (tunerBtn);

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

    // Motor do amp: CIRCUIT (primeira versão) e NEURAL (capturas dos amps reais)
    {
        const juce::StringArray en { "CIRCUIT", "NEURAL" };
        const juce::StringArray tips { "First version: circuit model",
                                       "Neural: captures of the real amps" };
        for (int i = 0; i < en.size(); ++i)
        {
            auto* b = engineButtons.add (new juce::TextButton (en[i]));
            b->setTooltip (tips[i]);
            b->onClick = [this, i]
            {
                if (auto* prm = proc.apvts.getParameter (ParamID::engine))
                {
                    prm->beginChangeGesture();
                    prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i));
                    prm->endChangeGesture();
                }
            };
            addAndMakeVisible (b);
        }
    }

    addAndMakeVisible (head);
    addChildComponent (cabPage);
    addChildComponent (pedalsPage);
    addChildComponent (fxPage);
    addChildComponent (tunerView);
    tunerBtn.setToggleState (proc.tunerActive.load(), juce::dontSendNotification);
    tunerView.setVisible (proc.tunerActive.load());

    setSize (1100, 720);
    showPage (PageAmp);
    startTimerHz (30);
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
    for (auto* b : engineButtons) b->setVisible (page == PageAmp);
    cabPage.setVisible (page == PageCab);
    pedalsPage.setVisible (page == PagePedals);
    fxPage.setVisible (page == PageFx);
    if (tunerView.isVisible()) tunerView.toFront (false);
    repaint();
}

juce::String ValvedEditor::tuningName (int semis)
{
    static const char* notes[] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    if (semis == 0) return "E Standard (no shift)";
    const int idx = ((4 + semis) % 12 + 12) % 12;
    const int a = std::abs (semis);
    juce::String amount = a == 12 ? juce::String ("1 octave")
                                  : juce::String (a) + (a == 1 ? " semitone" : " semitones");
    return juce::String (notes[idx]) + " Standard  (" + amount + (semis < 0 ? " down)" : " up)");
}

void ValvedEditor::timerCallback()
{
    const int ampIdx = (int) proc.apvts.getRawParameterValue (ParamID::amp)->load();
    for (int i = 0; i < ampButtons.size(); ++i)
        ampButtons[i]->setToggleState (i == ampIdx, juce::dontSendNotification);
    {
        const int eng = (int) proc.apvts.getRawParameterValue (ParamID::engine)->load();
        for (int i = 0; i < engineButtons.size(); ++i)
            engineButtons[i]->setToggleState (i == eng, juce::dontSendNotification);
    }
    head.setModel (ampIdx);
    head.refreshState();

    const int semis = (int) std::lround (proc.apvts.getRawParameterValue (ParamID::transpose)->load());
    juce::String tn = tuningName (semis);
    if (semis != 0 && proc.getSampleRate() > 0 && proc.getLatencySamples() > 0)
        tn += "   |   " + juce::String (1000.0 * proc.getLatencySamples() / proc.getSampleRate(), 0) + " ms latency";
    tuningLabel.setText (tn, juce::dontSendNotification);

    if (page == PageCab) cabPage.refresh();
    if (page == PagePedals) pedalsPage.refresh();
    if (page == PageFx) fxPage.refresh();
    if (tunerView.isVisible()) tunerView.tick();
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

    drawLogo (g, logoArea.toFloat());

    g.setColour (textDim);
    g.setFont (Fonts::condensed (14.0f));
    g.drawText ("v0.18", getLocalBounds().removeFromTop (58).reduced (24, 0), juce::Justification::centredRight);

    g.setFont (Fonts::condensedBold (12.0f));
    g.drawText ("IN", inMeter.getBounds().translated (0, inMeter.getHeight() + 2).withHeight (12).expanded (8, 0), juce::Justification::centred);
    g.drawText ("OUT", outMeter.getBounds().translated (0, outMeter.getHeight() + 2).withHeight (12).expanded (8, 0), juce::Justification::centred);

    // Fundo do palco (leve iluminação no centro)
    juce::ColourGradient stage (juce::Colour (0xff1d1d22), (float) mainArea.getCentreX(), (float) mainArea.getY() + 40.0f,
                                bg, (float) mainArea.getCentreX(), (float) mainArea.getBottom(), true);
    g.setGradientFill (stage);
    g.fillRect (mainArea);
}

void ValvedEditor::resized()
{
    auto r = getLocalBounds();
    topBar = r.removeFromTop (170);
    mainArea = r;

    auto row1 = juce::Rectangle<int> (0, 0, getWidth(), 58);
    logoArea = juce::Rectangle<int> (26, 11, 240, 36);
    auto tabs = row1.withSizeKeepingCentre (440, 58);
    for (auto* t : { &pedalsTab, &ampTab, &cabTab, &fxTab })
        t->setBounds (tabs.removeFromLeft (110));

    auto row2 = juce::Rectangle<int> (0, 66, getWidth(), 100).reduced (26, 0);
    input.setBounds (row2.removeFromLeft (92));
    row2.removeFromLeft (10);
    gate.setBounds (row2.removeFromLeft (92));
    row2.removeFromLeft (10);
    transpose.setBounds (row2.removeFromLeft (92));
    row2.removeFromLeft (8);
    tuningLabel.setBounds (row2.removeFromLeft (260).withSizeKeepingCentre (260, 24).translated (0, 4));

    auto meters = row2.removeFromRight (44).withTrimmedTop (14).withTrimmedBottom (18);
    inMeter.setBounds (meters.removeFromLeft (14));
    meters.removeFromLeft (14);
    outMeter.setBounds (meters.removeFromLeft (14));
    row2.removeFromRight (12);
    output.setBounds (row2.removeFromRight (92));
    row2.removeFromRight (24);
    tunerBtn.setBounds (row2.removeFromRight (104).withSizeKeepingCentre (104, 34).translated (0, 4));

    // Página AMP
    auto m = mainArea;
    // amps (4 x 130) + espaço + motores (2 x 104)
    auto sel = m.removeFromTop (56).withSizeKeepingCentre (4 * 130 + 3 * 8 + 28 + 2 * 104 + 6, 34).translated (0, 6);
    for (auto* b : ampButtons) { b->setBounds (sel.removeFromLeft (130)); sel.removeFromLeft (8); }
    sel.removeFromLeft (20);
    for (auto* b : engineButtons) { b->setBounds (sel.removeFromLeft (104)); sel.removeFromLeft (6); }
    head.setBounds (m.reduced (10, 4));

    // Página CAB
    cabPage.setBounds (mainArea.withTrimmedTop (16));
    pedalsPage.setBounds (mainArea);
    fxPage.setBounds (mainArea);
    tunerView.setBounds (mainArea);
}

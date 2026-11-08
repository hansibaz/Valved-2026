#include "CabPage.h"
#include "../PluginProcessor.h"

using namespace ui;

//==============================================================================
SpeakerView::SpeakerView (juce::AudioProcessorValueTreeState& s)
{
    px = s.getParameter (ParamID::micX);
    pd = s.getParameter (ParamID::micDist);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

juce::Point<float> SpeakerView::centre() const { return getLocalBounds().toFloat().getCentre(); }
float SpeakerView::coneRadius() const { return juce::jmin (getWidth(), getHeight()) * 0.5f * 0.80f; }

void SpeakerView::renderSpeaker()
{
    const float scale = 2.0f;
    background = juce::Image (juce::Image::ARGB, juce::jmax (1, (int) (getWidth() * scale)), juce::jmax (1, (int) (getHeight() * scale)), true);
    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (scale));

    const auto c = centre();
    const float R = juce::jmin (getWidth(), getHeight()) * 0.5f - 4.0f;
    auto circle = [&g, c] (float r) { g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2); };

    // Chassi (cesta) metálico
    juce::ColourGradient basket (juce::Colour (0xff3a3b3f), c.x, c.y - R, juce::Colour (0xff111113), c.x, c.y + R, false);
    g.setGradientFill (basket);
    circle (R);
    g.setColour (juce::Colours::black);
    for (int i = 0; i < 8; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / 8.0f + 0.2f;
        g.fillEllipse (c.x + std::sin (a) * R * 0.94f - 3.0f, c.y - std::cos (a) * R * 0.94f - 3.0f, 6.0f, 6.0f);
    }

    // Suspensão (borda de borracha)
    const float rs = R * 0.88f;
    juce::ColourGradient sur (juce::Colour (0xff2a2a2d), c.x - rs, c.y - rs, juce::Colour (0xff060606), c.x + rs, c.y + rs, false);
    g.setGradientFill (sur);
    circle (rs);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawEllipse (c.x - rs * 0.95f, c.y - rs * 0.95f, rs * 1.9f, rs * 1.9f, 2.0f);

    // Cone de papel
    const float rc = coneRadius();
    juce::ColourGradient cone (juce::Colour (0xff141415), c.x, c.y, juce::Colour (0xff2c2c2f), c.x, c.y - rc, true);
    cone.addColour (0.55, juce::Colour (0xff1d1d1f));
    g.setGradientFill (cone);
    circle (rc);
    juce::Random rng (11);
    for (int i = 0; i < 1800; ++i)
    {
        const float a = rng.nextFloat() * juce::MathConstants<float>::twoPi;
        const float rr = std::sqrt (rng.nextFloat()) * rc;
        g.setColour ((rng.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.05f));
        g.fillRect (c.x + std::sin (a) * rr, c.y - std::cos (a) * rr, 1.2f, 1.2f);
    }
    for (int i = 1; i <= 5; ++i)
    {
        const float rr = rc * (0.3f + 0.13f * (float) i);
        g.setColour (juce::Colours::white.withAlpha (0.035f));
        g.drawEllipse (c.x - rr, c.y - rr, rr * 2, rr * 2, 1.5f);
    }
    // Luz rasante (dá volume)
    juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.10f), c.x - rc * 0.6f, c.y - rc * 0.6f,
                                juce::Colours::transparentWhite, c.x + rc * 0.3f, c.y + rc * 0.3f, false);
    g.setGradientFill (sheen);
    circle (rc);

    // Calota central
    const float rd = rc * 0.27f;
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    circle (rd * 1.08f);
    juce::ColourGradient cap (juce::Colour (0xff4a4a4f), c.x - rd * 0.4f, c.y - rd * 0.5f, juce::Colour (0xff101012), c.x + rd, c.y + rd, true);
    g.setGradientFill (cap);
    circle (rd);
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.fillEllipse (c.x - rd * 0.55f, c.y - rd * 0.6f, rd * 0.6f, rd * 0.35f);
}

void SpeakerView::paint (juce::Graphics& g)
{
    if (background.isNull() || background.getWidth() != getWidth() * 2)
        renderSpeaker();
    g.drawImage (background, getLocalBounds().toFloat());

    if (! active)
    {
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (getLocalBounds().toFloat().reduced (4.0f));
        g.setColour (textDim);
        g.setFont (Fonts::condensed (18.0f));
        g.drawFittedText ("Mic position is part of your IR", getLocalBounds().reduced (60), juce::Justification::centred, 2);
        return;
    }

    // Microfone (visto de cima, apontado para o falante)
    const float mx = px->getValue(), md = pd->getValue();
    const auto c = centre();
    const float rc = coneRadius();
    const juce::Point<float> m (c.x + std::cos (angle) * mx * rc, c.y + std::sin (angle) * mx * rc);
    const float headR = rc * (0.16f - 0.06f * md);
    const float shadowOff = 6.0f + 26.0f * md;

    // Corpo do microfone (sai para baixo/direita)
    juce::Path body;
    body.addRoundedRectangle (-headR * 0.55f, 0.0f, headR * 1.1f, rc * 1.6f, headR * 0.3f);
    const auto bodyT = juce::AffineTransform::rotation (-0.5f).translated (m);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillPath (body, bodyT.translated (shadowOff, shadowOff));
    g.fillEllipse (m.x - headR + shadowOff, m.y - headR + shadowOff, headR * 2, headR * 2);
    juce::ColourGradient bg (juce::Colour (0xff2b2c30), m.x - headR, m.y, juce::Colour (0xff6c6f75), m.x + headR * 0.2f, m.y, false);
    bg.addColour (1.0, juce::Colour (0xff1c1d20));
    g.setGradientFill (bg);
    g.fillPath (body, bodyT);

    // Cabeça com tela
    juce::ColourGradient hg (juce::Colour (0xffb9bcc2), m.x - headR * 0.5f, m.y - headR * 0.6f, juce::Colour (0xff3b3d42), m.x + headR, m.y + headR, true);
    g.setGradientFill (hg);
    g.fillEllipse (m.x - headR, m.y - headR, headR * 2, headR * 2);
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip; clip.addEllipse (m.x - headR * 0.85f, m.y - headR * 0.85f, headR * 1.7f, headR * 1.7f);
        g.reduceClipRegion (clip);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        for (float x = m.x - headR; x < m.x + headR; x += 2.6f) g.fillRect (x, m.y - headR, 1.0f, headR * 2);
        for (float y = m.y - headR; y < m.y + headR; y += 2.6f) g.fillRect (m.x - headR, y, headR * 2, 1.0f);
    }
    g.setColour (accent);
    g.drawEllipse (m.x - headR - 3.0f, m.y - headR - 3.0f, headR * 2 + 6.0f, headR * 2 + 6.0f, 2.0f);
}

void SpeakerView::setFromMouse (juce::Point<float> p)
{
    const auto c = centre();
    const auto d = p - c;
    const float r = d.getDistanceFromOrigin();
    if (r > 1.0f) angle = std::atan2 (d.y, d.x);
    px->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, r / coneRadius()));
    repaint();
}

void SpeakerView::mouseDown (const juce::MouseEvent& e) { if (! active) return; px->beginChangeGesture(); setFromMouse (e.position); }
void SpeakerView::mouseDrag (const juce::MouseEvent& e) { if (active) setFromMouse (e.position); }
void SpeakerView::mouseUp (const juce::MouseEvent&)     { if (active) px->endChangeGesture(); }

//==============================================================================
CabPage::CabPage (ValvedProcessor& p) : proc (p), speaker (p.apvts)
{
    const auto names = vlv::getCabNames();
    for (int i = 0; i < names.size(); ++i)
    {
        auto* b = cabButtons.add (new juce::TextButton (names[i]));
        b->onClick = [this, i]
        {
            if (auto* prm = proc.apvts.getParameter (ParamID::cab))
            {
                if (i == vlv::CabUserIR && proc.getUserIRName().isEmpty()) { chooseIR(); return; }
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i));
                prm->endChangeGesture();
            }
        };
        addAndMakeVisible (b);
    }

    loadIR.setButtonText ("LOAD IR...");
    loadIR.onClick = [this] { chooseIR(); };
    addAndMakeVisible (loadIR);

    irName.setFont (Fonts::condensed (15.0f));
    irName.setColour (juce::Label::textColourId, textDim);
    irName.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (irName);

    addAndMakeVisible (cabOn);
    cabOnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, ParamID::cabOn, cabOn);

    distance.setSliderStyle (juce::Slider::LinearHorizontal);
    distance.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    distance.setColour (juce::Slider::trackColourId, accent);
    distance.setColour (juce::Slider::backgroundColourId, line);
    distance.setColour (juce::Slider::thumbColourId, text);
    addAndMakeVisible (distance);
    distanceAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, ParamID::micDist, distance);

    addAndMakeVisible (speaker);
}

CabPage::~CabPage() = default;

void CabPage::chooseIR()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose an impulse response (.wav)",
                                                   juce::File::getSpecialLocation (juce::File::userHomeDirectory),
                                                   "*.wav;*.aif;*.aiff");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile()) proc.loadUserIR (f);
                          });
}

void CabPage::refresh()
{
    const int cab = (int) proc.apvts.getRawParameterValue (ParamID::cab)->load();
    for (int i = 0; i < cabButtons.size(); ++i)
        cabButtons[i]->setToggleState (i == cab, juce::dontSendNotification);
    const auto name = proc.getUserIRName();
    irName.setText (name.isEmpty() ? juce::String ("No IR loaded") : name, juce::dontSendNotification);
    const bool user = cab == vlv::CabUserIR;
    speaker.setActive (! user);
    distance.setEnabled (! user);
    speaker.repaint();
}

void CabPage::paint (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRoundedRectangle (left.toFloat(), 10.0f);
    g.setColour (line);
    g.drawRoundedRectangle (left.toFloat().reduced (0.5f), 10.0f, 1.0f);

    auto l = left.reduced (22);
    g.setColour (textDim);
    g.setFont (Fonts::condensedBold (15.0f));
    g.drawText ("CABINET", l.removeFromTop (20), juce::Justification::centredLeft);

    g.drawText ("IMPULSE RESPONSE", juce::Rectangle<int> (left.getX() + 22, loadIR.getY() - 28, 200, 20), juce::Justification::centredLeft);
    g.drawText ("CAB / IR", juce::Rectangle<int> (cabOn.getRight() + 8, cabOn.getY(), 120, cabOn.getHeight()).withTrimmedTop (8), juce::Justification::centredLeft);

    // Rótulos da distância
    auto dl = distance.getBounds();
    g.setFont (Fonts::condensedBold (14.0f));
    g.drawText ("MIC DISTANCE", dl.withY (dl.getY() - 20).withHeight (18), juce::Justification::centred);
    g.setFont (Fonts::condensed (13.0f));
    g.drawText ("CLOSE", dl.withY (dl.getBottom()).withHeight (16), juce::Justification::centredLeft);
    g.drawText ("FAR", dl.withY (dl.getBottom()).withHeight (16), juce::Justification::centredRight);

    g.setColour (textDim.withAlpha (0.8f));
    g.setFont (Fonts::condensed (14.0f));
    auto sp = speaker.getBounds();
    g.drawText ("Drag the mic: center = brighter, edge = darker", sp.withY (sp.getY() - 24).withHeight (20), juce::Justification::centred);
}

void CabPage::resized()
{
    auto r = getLocalBounds().reduced (24, 12);
    left = r.removeFromLeft (300);
    r.removeFromLeft (24);
    right = r;

    auto l = left.reduced (22);
    l.removeFromTop (30);
    for (auto* b : cabButtons) { b->setBounds (l.removeFromTop (40)); l.removeFromTop (8); }
    l.removeFromTop (38);
    loadIR.setBounds (l.removeFromTop (40));
    l.removeFromTop (4);
    irName.setBounds (l.removeFromTop (24));
    l.removeFromTop (18);
    cabOn.setBounds (l.removeFromTop (70).removeFromLeft (56));

    auto rr = right;
    auto bottom = rr.removeFromBottom (60);
    rr.removeFromTop (26);
    const int s = juce::jmin (rr.getWidth(), rr.getHeight());
    speaker.setBounds (rr.withSizeKeepingCentre (s, s));
    distance.setBounds (bottom.withSizeKeepingCentre (juce::jmin (360, bottom.getWidth()), 22).translated (0, 4));
}

// Valved Cam Link: plugin "ponte". Colocado como ÚLTIMO plugin da trilha, ele deixa o som passar sem
// mexer em nada e manda esse som (de qualquer plugin de guitarra, de qualquer marca) para o app Valved Cam.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../CamLink.h"

namespace
{
const juce::Colour bg (0xff141216), panel (0xff1d1b20), line (0xff2c2a30), accent (0xfff0a045),
                   txt (0xffe9e6ea), dim (0xff8d8a92), ok (0xff59c97a);
}

class CamLinkProcessor : public juce::AudioProcessor
{
public:
    CamLinkProcessor()
        : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        link.setEnabled (true);   // a função do plugin é essa: já começa procurando o celular
        english = ! juce::SystemStats::getUserLanguage().startsWithIgnoreCase ("pt");   // idioma do sistema
    }

    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const auto out = l.getMainOutputChannelSet();
        if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
        return l.getMainInputChannelSet() == out;
    }

    void prepareToPlay (double sr, int) override { link.setSampleRate (sr); }
    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        const int n = buffer.getNumSamples();
        for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch) buffer.clear (ch, 0, n);
        if (buffer.getNumChannels() == 0 || n == 0) return;
        const float* L = buffer.getReadPointer (0);
        const float* R = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : nullptr;
        float pk = 0.0f;
        for (int i = 0; i < n; ++i) pk = juce::jmax (pk, std::abs (L[i]));
        level = juce::jmax (pk, level.load() * 0.85f);
        link.push (L, R, n);   // o som passa intacto; só uma cópia vai para o celular
    }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Valved Cam Link"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        juce::XmlElement x ("VALVEDCAMLINK");
        x.setAttribute ("enabled", link.isEnabled());
        x.setAttribute ("lang", english.load() ? "en" : "pt");
        copyXmlToBinary (x, dest);
    }
    void setStateInformation (const void* data, int size) override
    {
        if (auto x = getXmlFromBinary (data, size))
        {
            link.setEnabled (x->getBoolAttribute ("enabled", true));
            if (x->hasAttribute ("lang")) english = x->getStringAttribute ("lang") != "pt";
        }
    }

    vlv::CamLink link;
    std::atomic<float> level { 0.0f };
    std::atomic<bool> english { true };
};

class CamLinkEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit CamLinkEditor (CamLinkProcessor& p) : AudioProcessorEditor (p), proc (p)
    {
        setSize (420, 230);
        power.setClickingTogglesState (true);
        power.setToggleState (proc.link.isEnabled(), juce::dontSendNotification);
        power.onClick = [this] { proc.link.setEnabled (power.getToggleState()); repaint(); };
        power.setColour (juce::TextButton::buttonColourId, line);
        power.setColour (juce::TextButton::buttonOnColourId, accent);
        power.setColour (juce::TextButton::textColourOffId, txt);
        power.setColour (juce::TextButton::textColourOnId, bg);
        addAndMakeVisible (power);
        langBtn.setColour (juce::TextButton::buttonColourId, panel);
        langBtn.setColour (juce::TextButton::textColourOffId, dim);
        langBtn.onClick = [this] { proc.english = ! proc.english.load(); timerCallback(); };
        addAndMakeVisible (langBtn);
        startTimerHz (15);
        timerCallback();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (bg);
        auto r = getLocalBounds().reduced (16);

        // título
        auto top = r.removeFromTop (34);
        g.setColour (accent);
        g.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
        g.drawText ("VALVED", top.removeFromLeft (96), juce::Justification::centredLeft);
        g.setColour (txt);
        g.setFont (juce::Font (juce::FontOptions (22.0f)));
        g.drawText ("CAM LINK", top, juce::Justification::centredLeft);

        r.removeFromTop (8);
        auto card = r.removeFromTop (92).toFloat();
        g.setColour (panel); g.fillRoundedRectangle (card, 10.0f);
        g.setColour (line); g.drawRoundedRectangle (card.reduced (0.5f), 10.0f, 1.0f);

        auto c = card.reduced (14.0f, 10.0f);
        const auto st = proc.link.getStatus();
        const auto dotCol = st == vlv::CamLink::Connected ? ok : st == vlv::CamLink::Searching ? accent : dim;
        auto row = c.removeFromTop (24.0f);
        g.setColour (dotCol);
        g.fillEllipse (row.removeFromLeft (12.0f).withSizeKeepingCentre (10.0f, 10.0f));
        row.removeFromLeft (8.0f);
        g.setColour (txt);
        g.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
        g.drawText (st == vlv::CamLink::Connected ? t ("Connected to Valved Cam", "Conectado ao Valved Cam")
                    : st == vlv::CamLink::Searching ? t ("Looking for your phone...", "Procurando o celular...")
                    : t ("Off", "Desligado"), row, juce::Justification::centredLeft);

        g.setColour (dim);
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        auto info = c.removeFromTop (20.0f);
        if (st == vlv::CamLink::Connected)
            g.drawText (t ("Phone: ", "Celular: ") + proc.link.getDeviceName(), info, juce::Justification::centredLeft);
        else if (st == vlv::CamLink::Searching)
            g.drawText (t ("Open the Valved Cam app (same Wi-Fi or USB tethering)", "Abra o app Valved Cam (mesmo Wi-Fi ou ancoragem USB)"), info, juce::Justification::centredLeft);
        else
            g.drawText (t ("Turn it on to send the sound to your phone", "Ligue para mandar o som para o celular"), info, juce::Justification::centredLeft);

        // medidor do som que está indo
        auto meter = c.removeFromTop (22.0f).reduced (0.0f, 6.0f);
        g.setColour (line); g.fillRoundedRectangle (meter, 4.0f);
        const float lv = juce::jlimit (0.0f, 1.0f, std::sqrt (proc.level.load()));
        g.setColour (st == vlv::CamLink::Connected ? ok : dim);
        g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * lv), 4.0f);

        r.removeFromTop (10);
        auto bottom = r.removeFromTop (40);
        bottom.removeFromLeft (130);
        g.setColour (dim);
        g.setFont (juce::Font (juce::FontOptions (12.5f)));
        g.drawFittedText (t ("Insert it as the LAST plugin on the track. The sound passes through untouched.",
                             "Coloque como o ÚLTIMO plugin da trilha. O som passa sem nenhuma alteração."),
                          bottom.reduced (10, 0), juce::Justification::centredLeft, 2);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        r.removeFromTop (34 + 8 + 92 + 10);
        power.setBounds (r.removeFromTop (40).removeFromLeft (120));
        langBtn.setBounds (getWidth() - 16 - 64, 20, 64, 24);
    }

private:
    void timerCallback() override
    {
        power.setButtonText (proc.link.isEnabled() ? t ("ON", "LIGADO") : t ("OFF", "DESLIGADO"));
        langBtn.setButtonText (proc.english.load() ? "EN | pt" : "en | PT");
        repaint();
    }

    juce::String t (const char* en, const char* pt) const
    {
        return juce::String (juce::CharPointer_UTF8 (proc.english.load() ? en : pt));
    }

    CamLinkProcessor& proc;
    juce::TextButton power, langBtn;
};

juce::AudioProcessorEditor* CamLinkProcessor::createEditor() { return new CamLinkEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CamLinkProcessor(); }

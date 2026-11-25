// Valved Cam Link: plugin "ponte". Colocado como ÚLTIMO plugin da trilha, ele deixa o som passar sem
// mexer em nada e manda esse som (de qualquer plugin de guitarra, de qualquer marca) para o app Valved Cam.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_cryptography/juce_cryptography.h>
#include "../CamLink.h"
#include "../License.h"

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
        english = ! juce::SystemStats::getUserLanguage().startsWithIgnoreCase ("pt");   // idioma do sistema
        license.reload();
        applyEnable (true);   // a função do plugin é essa: já começa procurando o celular (se estiver ativado)
    }

    // só conecta no celular com a licença ativada
    void applyEnable (bool want) { wantEnabled = want; link.setEnabled (want && license.isActive()); }

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
        if (license.isActive())
            link.push (L, R, n);   // o som passa intacto; só uma cópia vai para o celular (só com a licença ativada)
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
        x.setAttribute ("enabled", wantEnabled.load());
        x.setAttribute ("lang", english.load() ? "en" : "pt");
        copyXmlToBinary (x, dest);
    }
    void setStateInformation (const void* data, int size) override
    {
        if (auto x = getXmlFromBinary (data, size))
        {
            applyEnable (x->getBoolAttribute ("enabled", true));
            if (x->hasAttribute ("lang")) english = x->getStringAttribute ("lang") != "pt";
        }
    }

    vlv::CamLink link;
    std::atomic<float> level { 0.0f };
    std::atomic<bool> english { true };
    vlv::License license;
    std::atomic<bool> wantEnabled { true };
};

class CamLinkEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit CamLinkEditor (CamLinkProcessor& p) : AudioProcessorEditor (p), proc (p)
    {
        setSize (420, 300);
        for (auto* e : { &emailEd, &keyEd })
        {
            e->setColour (juce::TextEditor::backgroundColourId, panel);
            e->setColour (juce::TextEditor::textColourId, txt);
            e->setColour (juce::TextEditor::outlineColourId, line);
            e->setColour (juce::TextEditor::focusedOutlineColourId, accent);
            e->setColour (juce::CaretComponent::caretColourId, accent);
            e->setIndents (10, 7);
            addChildComponent (*e);
        }
        emailEd.setFont (juce::Font (juce::FontOptions (15.0f)));
        keyEd.setMultiLine (true, true);
        keyEd.setReturnKeyStartsNewLine (false);
        keyEd.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain)));
        for (auto* b : { &pasteBtn, &activateBtn })
        {
            b->setColour (juce::TextButton::buttonColourId, b == &activateBtn ? accent : line);
            b->setColour (juce::TextButton::textColourOffId, b == &activateBtn ? bg : txt);
            addChildComponent (*b);
        }
        pasteBtn.onClick = [this] { keyEd.setText (juce::SystemClipboard::getTextFromClipboard().trim(), false); };
        activateBtn.onClick = [this] { tryActivate(); };
        emailEd.onReturnKey = keyEd.onReturnKey = [this] { tryActivate(); };
        power.setClickingTogglesState (true);
        power.setToggleState (proc.wantEnabled.load(), juce::dontSendNotification);
        power.onClick = [this] { proc.applyEnable (power.getToggleState()); repaint(); };
        power.setColour (juce::TextButton::buttonColourId, line);
        power.setColour (juce::TextButton::buttonOnColourId, accent);
        power.setColour (juce::TextButton::textColourOffId, txt);
        power.setColour (juce::TextButton::textColourOnId, bg);
        addAndMakeVisible (power);
        langBtn.setColour (juce::TextButton::buttonColourId, panel);
        langBtn.setColour (juce::TextButton::textColourOffId, dim);
        langBtn.onClick = [this] { proc.english = ! proc.english.load(); timerCallback(); };
        addAndMakeVisible (langBtn);
        lastEnglish = proc.english.load(); lastLocked = locked();
        updateMode();
        startTimerHz (15);
        timerCallback();
    }

    bool locked() const { return ! proc.license.isActive(); }

    void paintActivation (juce::Graphics& g, juce::Rectangle<int> r)
    {
        g.setColour (accent);
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.drawText (t ("PRODUCT ACTIVATION", "ATIVAÇÃO DO PRODUTO"), r.removeFromTop (20), juce::Justification::centredLeft);
        g.setColour (dim);
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawFittedText (t ("Enter the e-mail you used to buy and the license key you received.",
                             "Digite o e-mail da compra e a chave de licença que você recebeu."),
                          r.removeFromTop (34), juce::Justification::topLeft, 2);
        g.setColour (activationOk ? ok : juce::Colour (0xffe2523a));
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawFittedText (status, getLocalBounds().reduced (16).removeFromBottom (22), juce::Justification::centredLeft, 1);
    }

    void tryActivate()
    {
        if (proc.license.activate (emailEd.getText(), keyEd.getText()))
        {
            activationOk = true; status = t ("Activated! Enjoy Valved Cam Link.", "Ativado! Bom uso do Valved Cam Link.");
            proc.applyEnable (proc.wantEnabled.load());
            updateMode();
        }
        else
        {
            activationOk = false;
            status = t ("This key doesn't match this e-mail. Check both against the purchase e-mail.",
                        "Essa chave não confere com esse e-mail. Confira os dois no e-mail da compra.");
        }
        repaint();
    }

    void updateMode()
    {
        const bool lk = locked();
        for (juce::Component* c : { (juce::Component*) &emailEd, (juce::Component*) &keyEd, (juce::Component*) &pasteBtn, (juce::Component*) &activateBtn })
            c->setVisible (lk);
        power.setVisible (! lk);
        emailEd.setTextToShowWhenEmpty (t ("Purchase e-mail", "E-mail da compra"), dim);
        keyEd.setTextToShowWhenEmpty (t ("Paste your license key here", "Cole aqui a sua chave de licença"), dim);
        pasteBtn.setButtonText (t ("PASTE KEY", "COLAR CHAVE"));
        activateBtn.setButtonText (t ("ACTIVATE", "ATIVAR"));
        resized();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (bg);
        auto r = getLocalBounds().reduced (16);
        if (locked())
        {
            auto top0 = r.removeFromTop (34);
            g.setColour (accent);
            g.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
            g.drawText ("VALVED", top0.removeFromLeft (96), juce::Justification::centredLeft);
            g.setColour (txt);
            g.setFont (juce::Font (juce::FontOptions (22.0f)));
            g.drawText ("CAM LINK", top0, juce::Justification::centredLeft);
            r.removeFromTop (6);
            paintActivation (g, r);
            return;
        }

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
        langBtn.setBounds (getWidth() - 16 - 64, 20, 64, 24);
        if (locked())
        {
            auto a = getLocalBounds().reduced (16);
            a.removeFromTop (34 + 6 + 20 + 34);
            emailEd.setBounds (a.removeFromTop (32));
            a.removeFromTop (8);
            keyEd.setBounds (a.removeFromTop (64));
            a.removeFromTop (8);
            auto row = a.removeFromTop (34);
            pasteBtn.setBounds (row.removeFromLeft (130));
            row.removeFromLeft (10);
            activateBtn.setBounds (row);
            return;
        }
        auto r = getLocalBounds().reduced (16);
        r.removeFromTop (34 + 8 + 92 + 10);
        power.setBounds (r.removeFromTop (40).removeFromLeft (120));
    }

private:
    void timerCallback() override
    {
        power.setButtonText (proc.wantEnabled.load() ? t ("ON", "LIGADO") : t ("OFF", "DESLIGADO"));
        const bool en = proc.english.load();
        if (en != lastEnglish || locked() != lastLocked) { lastEnglish = en; lastLocked = locked(); updateMode(); }
        if (locked() && ++reloadTick % 30 == 0 && proc.license.reload()) { proc.applyEnable (proc.wantEnabled.load()); updateMode(); }   // ativado em outra instância
        langBtn.setButtonText (proc.english.load() ? "EN | pt" : "en | PT");
        repaint();
    }

    juce::String t (const char* en, const char* pt) const
    {
        return juce::String (juce::CharPointer_UTF8 (proc.english.load() ? en : pt));
    }

    CamLinkProcessor& proc;
    juce::TextButton power, langBtn, pasteBtn, activateBtn;
    juce::TextEditor emailEd, keyEd;
    juce::String status;
    bool activationOk = false, lastEnglish = true, lastLocked = false;
    int reloadTick = 0;
};

juce::AudioProcessorEditor* CamLinkProcessor::createEditor() { return new CamLinkEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CamLinkProcessor(); }

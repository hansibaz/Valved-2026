#pragma once
// Tela de ativação: cobre o plugin inteiro enquanto não houver licença válida.
// O cliente cola o e-mail da compra e a chave que recebeu; se estiver certo, a tela some e o som volta.
#include <juce_gui_basics/juce_gui_basics.h>
#include "Style.h"
#include "../License.h"
#include "BinaryData.h"

class ActivationView : public juce::Component, private juce::Timer
{
public:
    // botão principal laranja (desenhado aqui para não depender do LookAndFeel)
    struct CtaButton : public juce::Button
    {
        CtaButton() : juce::Button ("cta") { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            juce::ColourGradient gr (juce::Colour (0xffffc164), r.getX(), r.getY(), juce::Colour (0xffe88a2c), r.getX(), r.getBottom(), false);
            g.setGradientFill (gr); g.fillRoundedRectangle (r, 10.0f);
            if (over || down) { g.setColour (juce::Colours::white.withAlpha (down ? 0.18f : 0.10f)); g.fillRoundedRectangle (r, 10.0f); }
            g.setColour (juce::Colour (0xff17120a));
            g.setFont (ui::Fonts::condensedBold (21.0f));
            g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
        }
    };

    ActivationView (vlv::License& l, std::function<void()> onActivated) : license (l), activated (std::move (onActivated))
    {
        for (auto* e : { &emailEd, &keyEd })
        {
            e->setFont (ui::Fonts::condensedMedium (19.0f));
            e->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0e0e11));
            e->setColour (juce::TextEditor::textColourId, ui::text);
            e->setColour (juce::TextEditor::outlineColourId, ui::line);
            e->setColour (juce::TextEditor::focusedOutlineColourId, ui::accent);
            e->setColour (juce::TextEditor::highlightColourId, ui::accent.withAlpha (0.35f));
            e->setColour (juce::CaretComponent::caretColourId, ui::accent);
            e->setIndents (12, 10);
            e->getProperties().set ("noTr", true);
            addAndMakeVisible (*e);
        }
        keyEd.setMultiLine (true, true);
        keyEd.setReturnKeyStartsNewLine (false);
        keyEd.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain)));
        emailEd.onReturnKey = [this] { tryActivate(); };
        keyEd.onReturnKey = [this] { tryActivate(); };

        activateBtn.getProperties().set ("noTr", true);
        activateBtn.onClick = [this] { tryActivate(); };
        addAndMakeVisible (activateBtn);

        pasteBtn.getProperties().set ("noTr", true);
        pasteBtn.onClick = [this] { keyEd.setText (juce::SystemClipboard::getTextFromClipboard().trim(), false); };
        addAndMakeVisible (pasteBtn);

        refreshLanguage();
        startTimer (2000);   // outra instância (ou o standalone) pode ter ativado: confere o arquivo de vez em quando
    }

    void refreshLanguage()
    {
        emailEd.setTextToShowWhenEmpty (lang::t ("E-mail used in the purchase", "E-mail usado na compra"), ui::textDim);
        keyEd.setTextToShowWhenEmpty (lang::t ("Paste your license key here", "Cole aqui a sua chave de licença"), ui::textDim);
        activateBtn.setButtonText (lang::t ("ACTIVATE", "ATIVAR"));
        pasteBtn.setButtonText (lang::t ("PASTE KEY", "COLAR CHAVE"));
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.82f));
        auto card = cardBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (card.translated (0, 14).expanded (6), 22.0f);
        juce::ColourGradient grad (juce::Colour (0xff1c1a18), card.getX(), card.getY(), juce::Colour (0xff0d0d10), card.getRight(), card.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (card, 18.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (card.reduced (0.5f), 18.0f, 1.0f);

        static const juce::Image logo = juce::ImageCache::getFromMemory (BinaryData::valved_logo_png, BinaryData::valved_logo_pngSize);
        auto inner = card.reduced (44.0f, 34.0f);
        if (logo.isValid())
        {
            const float lh = 46.0f, lw = lh * (float) logo.getWidth() / (float) logo.getHeight();
            g.drawImage (logo, juce::Rectangle<float> (inner.getCentreX() - lw / 2, inner.getY(), lw, lh), juce::RectanglePlacement::centred);
        }
        g.setColour (ui::accent);
        g.setFont (ui::Fonts::condensed (16.0f));
        g.drawText (lang::t ("PRODUCT ACTIVATION", "ATIVAÇÃO DO PRODUTO"), inner.withY (inner.getY() + 62).withHeight (20).toNearestInt(), juce::Justification::centred);
        g.setColour (ui::text.withAlpha (0.85f));
        g.setFont (ui::Fonts::condensedMedium (18.0f));
        g.drawFittedText (lang::t ("Enter the e-mail you used to buy and the key you received to unlock the sound.",
                                   "Digite o e-mail da compra e a chave que você recebeu para liberar o som."),
                          inner.withY (inner.getY() + 88).withHeight (44).toNearestInt(), juce::Justification::centred, 2);

        if (status.isNotEmpty())
        {
            g.setColour (statusOk ? juce::Colour (0xff59c97a) : juce::Colour (0xffe2523a));
            g.setFont (ui::Fonts::condensed (17.0f));
            g.drawFittedText (status, statusArea, juce::Justification::centred, 2);
        }
        g.setColour (ui::textDim);
        g.setFont (ui::Fonts::condensedMedium (15.0f));
        g.drawFittedText (lang::t ("Bought it and didn't get your key? Check your spam folder or reply to the purchase e-mail.",
                                   "Comprou e não recebeu a chave? Veja a caixa de spam ou responda o e-mail da compra."),
                          footArea, juce::Justification::centred, 2);
    }

    void resized() override
    {
        auto inner = cardBounds().reduced (44, 34);
        inner.removeFromTop (142);
        emailEd.setBounds (inner.removeFromTop (42));
        inner.removeFromTop (12);
        keyEd.setBounds (inner.removeFromTop (96));
        inner.removeFromTop (14);
        auto row = inner.removeFromTop (46);
        pasteBtn.setBounds (row.removeFromLeft (150));
        row.removeFromLeft (12);
        activateBtn.setBounds (row);
        inner.removeFromTop (8);
        statusArea = inner.removeFromTop (40);
        footArea = inner.removeFromBottom (40);
    }

private:
    juce::Rectangle<int> cardBounds() const { return getLocalBounds().withSizeKeepingCentre (560, 540); }

    void tryActivate()
    {
        if (license.activate (emailEd.getText(), keyEd.getText()))
        {
            status = lang::t ("Activated! Enjoy the Valved.", "Ativado! Bom som com o Valved."); statusOk = true; repaint();
            if (activated) activated();
        }
        else
        {
            status = lang::t ("This key doesn't match this e-mail. Check that both are exactly the ones from the purchase e-mail.",
                              "Essa chave não confere com esse e-mail. Confira se os dois estão iguais aos do e-mail da compra.");
            statusOk = false; repaint();
        }
    }

    void timerCallback() override
    {
        if (license.reload() && activated) activated();
    }

    vlv::License& license;
    std::function<void()> activated;
    juce::TextEditor emailEd, keyEd;
    CtaButton activateBtn;
    juce::TextButton pasteBtn { "COLAR" };
    juce::String status; bool statusOk = false;
    juce::Rectangle<int> statusArea, footArea;
};

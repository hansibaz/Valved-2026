#pragma once
// Tela do afinador (cobre a área principal quando o TUNER está ligado).
//  - Nota grande com a oitava, frequência em Hz e desvio em cents (décimos).
//  - Ponteiro de -50 a +50 cents com zona verde (±2 cents) e marcações a cada 5 cents.
//  - Faixa "strobe": as listras andam para a direita quando a nota está alta e para a esquerda
//    quando está baixa, e param quando está afinada (como os afinadores strobe de bancada:
//    mostra até frações de cent).
//  - A4 ajustável (430-450 Hz) e chave MUTE (afinar em silêncio).
#include <juce_gui_basics/juce_gui_basics.h>
#include "Style.h"
#include "../PluginProcessor.h"

class TunerView : public juce::Component
{
public:
    explicit TunerView (ValvedProcessor& p) : proc (p)
    {
        for (auto* b : { &refDown, &refUp, &muteBtn })
            addAndMakeVisible (b);
        muteBtn.setClickingTogglesState (true);
        muteBtn.setToggleState (proc.tunerMute.load(), juce::dontSendNotification);
        muteBtn.onClick = [this] { proc.tunerMute = muteBtn.getToggleState(); };
        muteBtn.setTooltip ("Silence the output while tuning");
        refDown.onClick = [this] { proc.tunerRef = juce::jlimit (430.0, 450.0, proc.tunerRef.load() - 1.0); repaint(); };
        refUp.onClick   = [this] { proc.tunerRef = juce::jlimit (430.0, 450.0, proc.tunerRef.load() + 1.0); repaint(); };
    }

    // chamado pelo timer da interface (~30x por segundo)
    void tick()
    {
        muteBtn.setToggleState (proc.tunerMute.load(), juce::dontSendNotification);
        const auto r = analyzer.analyse (proc.tunerFeed, proc.tunerRef.load());
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const double dt = lastTick > 0 ? juce::jlimit (0.0, 0.1, now - lastTick) : 0.0;
        lastTick = now;
        if (r.valid)
        {
            reading = r; holdUntil = now + 0.6;
            // ponteiro: amortecimento leve (o analisador já estabiliza)
            needle += 0.6 * (r.cents - needle);
            if (! shown) needle = r.cents;
            shown = true;
            // strobe: velocidade proporcional ao desvio (1 cent = listras andando devagar)
            strobePhase += dt * r.cents * 0.6;
            strobePhase -= std::floor (strobePhase);
        }
        else if (now > holdUntil)
        {
            shown = false;
        }
        repaint();
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (40, 24);
        auto bottom = b.removeFromBottom (44);
        auto refArea = bottom.removeFromLeft (260);
        refDown.setBounds (refArea.removeFromLeft (40).withSizeKeepingCentre (36, 32));
        refUp.setBounds (refArea.removeFromRight (40).withSizeKeepingCentre (36, 32));
        refLabel = refArea;
        muteBtn.setBounds (bottom.removeFromRight (120).withSizeKeepingCentre (110, 34));
    }

    void paint (juce::Graphics& g) override
    {
        using namespace ui;
        auto area = getLocalBounds().toFloat();
        g.setColour (bg.withAlpha (0.96f));
        g.fillRect (area);

        auto panelR = area.reduced (24.0f, 12.0f).withTrimmedBottom (56.0f);
        g.setColour (panel);
        g.fillRoundedRectangle (panelR, 10.0f);
        g.setColour (line);
        g.drawRoundedRectangle (panelR, 10.0f, 1.0f);

        const bool inTune = shown && std::abs (reading.cents) < 2.0;
        const juce::Colour green (0xff4cd37a), red (0xffff4a3d);
        const juce::Colour stateCol = ! shown ? textDim : (inTune ? green : (std::abs (reading.cents) < 10.0 ? accent : red));

        auto inner = panelR.reduced (30.0f, 18.0f);
        g.setFont (Fonts::condensedBold (14.0f));
        g.setColour (textDim);
        g.drawText ("TUNER", inner.removeFromTop (18.0f), juce::Justification::centredLeft);

        // ---- Nota
        auto noteArea = inner.removeFromTop (150.0f);
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        if (shown)
        {
            const int midi = reading.midi;
            const juce::String nm = names[((midi % 12) + 12) % 12];
            const juce::String oct (midi / 12 - 1);
            g.setColour (stateCol);
            g.setFont (Fonts::condensedBold (140.0f));
            const float w = g.getCurrentFont().getStringWidthFloat (nm);
            auto nr = noteArea.withSizeKeepingCentre (w + 4.0f, noteArea.getHeight());
            g.drawText (nm, nr, juce::Justification::centred);
            g.setFont (Fonts::condensedBold (40.0f));
            g.drawText (oct, nr.translated (w * 0.5f + 26.0f, 30.0f).withWidth (40.0f), juce::Justification::centredLeft);

            // flechas: abaixe / suba
            g.setFont (Fonts::condensedBold (54.0f));
            g.setColour (reading.cents > 2.0 ? red : panelHi);
            g.drawText (juce::CharPointer_UTF8 ("\xe2\x97\x80"), noteArea.withTrimmedRight (noteArea.getWidth() * 0.5f + 150.0f), juce::Justification::centredRight);
            g.setColour (reading.cents < -2.0 ? red : panelHi);
            g.drawText (juce::CharPointer_UTF8 ("\xe2\x96\xb6"), noteArea.withTrimmedLeft (noteArea.getWidth() * 0.5f + 150.0f), juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (panelHi);
            g.setFont (Fonts::condensedBold (140.0f));
            g.drawText ("-", noteArea, juce::Justification::centred);
        }

        // ---- Leituras (Hz e cents)
        auto info = inner.removeFromTop (30.0f);
        g.setFont (Fonts::condensed (22.0f));
        g.setColour (shown ? text : textDim);
        const juce::String hz = shown ? juce::String (reading.freq, 2) + " Hz" : juce::String ("PLAY A SINGLE NOTE");
        const juce::String ct = shown ? (reading.cents >= 0 ? "+" : "") + juce::String (reading.cents, 1) + " cents" : juce::String();
        g.drawText (hz, info.withTrimmedRight (info.getWidth() * 0.5f + 20.0f), juce::Justification::centredRight);
        g.setColour (shown ? stateCol : textDim);
        g.drawText (ct, info.withTrimmedLeft (info.getWidth() * 0.5f + 20.0f), juce::Justification::centredLeft);
        inner.removeFromTop (14.0f);

        // ---- Escala de cents com ponteiro
        auto scale = inner.removeFromTop (70.0f).reduced (40.0f, 0.0f);
        const float cx = scale.getCentreX(), half = scale.getWidth() * 0.5f;
        auto xOf = [&] (double c) { return cx + (float) (juce::jlimit (-50.0, 50.0, c) / 50.0) * half; };
        g.setColour (green.withAlpha (0.18f));
        g.fillRoundedRectangle (juce::Rectangle<float> (xOf (-2.0), scale.getY(), xOf (2.0) - xOf (-2.0), scale.getHeight() - 18.0f), 3.0f);
        for (int c = -50; c <= 50; c += 5)
        {
            const float x = xOf (c);
            const bool major = c % 25 == 0;
            g.setColour (c == 0 ? green : (major ? text.withAlpha (0.7f) : textDim.withAlpha (0.6f)));
            const float h = c == 0 ? 44.0f : (major ? 30.0f : 16.0f);
            g.fillRect (x - (c == 0 ? 1.5f : 0.75f), scale.getY() + (52.0f - h) * 0.5f, c == 0 ? 3.0f : 1.5f, h);
            if (major)
            {
                g.setFont (Fonts::condensed (13.0f));
                g.setColour (textDim);
                g.drawText (c > 0 ? "+" + juce::String (c) : juce::String (c), juce::Rectangle<float> (x - 20.0f, scale.getBottom() - 16.0f, 40.0f, 16.0f), juce::Justification::centred);
            }
        }
        if (shown)
        {
            const float x = xOf (needle);
            g.setColour (stateCol);
            g.fillRoundedRectangle (x - 2.5f, scale.getY() - 6.0f, 5.0f, 64.0f, 2.5f);
            juce::Path tri;
            tri.addTriangle (x - 8.0f, scale.getY() - 14.0f, x + 8.0f, scale.getY() - 14.0f, x, scale.getY() - 4.0f);
            g.fillPath (tri);
        }
        inner.removeFromTop (16.0f);

        // ---- Faixa strobe
        auto strobe = inner.removeFromTop (34.0f).reduced (40.0f, 0.0f);
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (strobe, 4.0f);
        if (shown)
        {
            g.saveState();
            g.reduceClipRegion (strobe.toNearestInt());
            const float period = 36.0f;
            const float off = (float) strobePhase * period;
            for (float x = strobe.getX() - period + off; x < strobe.getRight(); x += period)
            {
                g.setColour (stateCol.withAlpha (0.85f));
                g.fillRect (x, strobe.getY(), period * 0.5f, strobe.getHeight());
            }
            g.restoreState();
        }
        g.setColour (line);
        g.drawRoundedRectangle (strobe, 4.0f, 1.0f);

        // ---- Referência A4
        g.setColour (text);
        g.setFont (Fonts::condensed (18.0f));
        g.drawText ("A4 = " + juce::String ((int) std::lround (proc.tunerRef.load())) + " Hz", refLabel, juce::Justification::centred);
    }

private:
    ValvedProcessor& proc;
    vlv::TunerAnalyzer analyzer;
    vlv::TunerAnalyzer::Reading reading;
    juce::TextButton refDown { "-" }, refUp { "+" }, muteBtn { "MUTE" };
    juce::Rectangle<int> refLabel;
    double needle = 0.0, strobePhase = 0.0, lastTick = 0.0, holdUntil = 0.0;
    bool shown = false;
};

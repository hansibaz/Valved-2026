#pragma once
// Barra BACKING TRACK (embaixo do plugin): carregar uma base, tocar/pausar/parar, repetir,
// forma de onda com o ponto de reprodução (clique para pular), volume da base e
// "NO VÍDEO" (a base vai junto para o app Valved Cam).
#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "Style.h"
#include "Lang.h"

class BackingBar : public juce::Component,
                   public juce::FileDragAndDropTarget,
                   private juce::Timer,
                   private juce::ChangeListener
{
public:
    explicit BackingBar (ValvedProcessor& p) : proc (p), bt (p.backing), wave (*this)
    {
        auto setupBtn = [this] (juce::TextButton& b)
        {
            b.getProperties().set ("noTr", true);
            b.setMouseCursor (juce::MouseCursor::PointingHandCursor);
            addAndMakeVisible (b);
        };
        setupBtn (openBtn);
        addAndMakeVisible (clearBtn);
        openBtn.onClick = [this] { chooseFile(); };
        clearBtn.onClick = [this] { bt.clear(); refresh(); };

        addAndMakeVisible (playBtn);
        addAndMakeVisible (stopBtn);
        addAndMakeVisible (loopBtn);
        playBtn.onClick = [this] { if (bt.isPlaying()) bt.pause(); else if (bt.hasFile()) bt.play(); else chooseFile(); refresh(); };
        stopBtn.onClick = [this] { bt.stop(); refresh(); };
        loopBtn.onClick = [this] { bt.setLoop (! bt.getLoop()); refresh(); };

        camBtn.onClick = [this] { bt.sendToCam = ! bt.sendToCam.load(); refresh(); };
        addAndMakeVisible (camBtn);

        vol.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        vol.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        vol.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        vol.setMouseDragSensitivity (220);
        vol.getProperties().set ("knobStyle", (int) ui::KnobModern);
        vol.setRange (-60.0, 6.0, 0.1);
        vol.setSkewFactorFromMidPoint (-12.0);
        vol.setDoubleClickReturnValue (true, -6.0);
        vol.setValue (bt.volumeDb.load(), juce::dontSendNotification);
        vol.onValueChange = [this] { bt.volumeDb = (float) vol.getValue(); repaint (volArea); };
        addAndMakeVisible (vol);

        addAndMakeVisible (wave);
        bt.getThumbnail().addChangeListener (this);
        refreshLanguage();
        startTimerHz (30);
    }

    ~BackingBar() override { bt.getThumbnail().removeChangeListener (this); }

    void refreshLanguage()
    {
        openBtn.setButtonText (lang::t ("OPEN", "ABRIR"));
        clearBtn.setTooltip (lang::t ("Remove the backing track", "Tirar a base"));
        playBtn.setTooltip (lang::t ("Play / pause (Space)", "Tocar / pausar (barra de espaço)"));
        stopBtn.setTooltip (lang::t ("Stop and go back to the start", "Parar e voltar para o começo"));
        loopBtn.setTooltip (lang::t ("Loop: start over when it ends", "Repetir: volta para o começo quando acabar"));
        camBtn.setTooltip (lang::t ("ON: the backing track goes into the Valved Cam video together with your guitar.\nOFF: only the guitar goes to the phone (you still hear the backing track).",
                                    "LIGADO: a base vai para o vídeo do Valved Cam junto com a guitarra.\nDESLIGADO: só a guitarra vai para o celular (você continua ouvindo a base)."));
        vol.setTooltip (lang::t ("Backing track volume (double-click: -6 dB)", "Volume da base (dois cliques: -6 dB)"));
        refresh();
    }

    // ---------------------------------------------------------------- layout e desenho
    void resized() override
    {
        auto r = getLocalBounds().reduced (24, 0);
        auto left = r.removeFromLeft (214);
        titleArea = left.withHeight (16).withY (16);
        nameArea = left.withHeight (22).withY (34);
        openBtn.setBounds (left.getX(), 66, 92, 28);
        clearBtn.setBounds (left.getX() + 98, 66, 28, 28);

        r.removeFromLeft (14);
        auto tr = r.removeFromLeft (164);
        playBtn.setBounds (tr.getX(), 26, 58, 58);
        stopBtn.setBounds (tr.getX() + 70, 37, 36, 36);
        loopBtn.setBounds (tr.getX() + 114, 37, 36, 36);

        auto right = r.removeFromRight (236);
        camBtn.setBounds (right.removeFromRight (118).withSizeKeepingCentre (118, 34).translated (0, 2));
        right.removeFromRight (18);
        volArea = right.removeFromRight (90);
        vol.setBounds (volArea.withTrimmedTop (22).withTrimmedBottom (20).withSizeKeepingCentre (58, 58));

        r.removeFromRight (12);
        wave.setBounds (r.withY (16).withHeight (getHeight() - 32));
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        juce::ColourGradient grad (juce::Colour (0xff151519), 0, 0, juce::Colour (0xff0e0e11), 0, b.getHeight(), false);
        g.setGradientFill (grad);
        g.fillRect (b);
        g.setColour (ui::line);
        g.fillRect (0.0f, 0.0f, b.getWidth(), 1.0f);
        g.setColour (ui::accent.withAlpha (0.35f));
        g.fillRect (b.getWidth() * 0.5f - 60.0f, 0.0f, 120.0f, 1.0f);

        // título e nome do arquivo
        g.setColour (ui::accent);
        g.setFont (ui::Fonts::condensedBold (14.0f));
        g.drawText ("BACKING TRACK", titleArea, juce::Justification::centredLeft);
        g.setColour (bt.hasFile() ? ui::text : ui::textDim);
        g.setFont (ui::Fonts::condensed (bt.hasFile() ? 17.0f : 15.0f));
        const auto name = bt.hasFile() ? bt.getFile().getFileNameWithoutExtension() : lang::t ("No backing track loaded", "Nenhuma base carregada");
        g.drawFittedText (name, nameArea, juce::Justification::centredLeft, 1, 0.85f);

        // volume
        g.setColour (ui::text);
        g.setFont (ui::Fonts::condensedBold (14.0f));
        g.drawText (lang::t ("VOLUME", "VOLUME"), volArea.withHeight (18).withY (8), juce::Justification::centred);
        g.setColour (ui::textDim.brighter (0.4f));
        g.setFont (ui::Fonts::condensed (14.0f));
        const float db = bt.volumeDb.load();
        g.drawText (db <= -59.5f ? juce::String (juce::CharPointer_UTF8 ("-\xe2\x88\x9e dB")) : juce::String (db, 1) + " dB",
                    volArea.withHeight (18).withY (getHeight() - 26), juce::Justification::centred);
    }

    // ---------------------------------------------------------------- arrastar arquivo
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (auto& f : files) if (isAudio (f)) return true;
        return false;
    }
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; wave.repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; wave.repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        dragOver = false;
        for (auto& f : files) if (isAudio (f)) { loadFile (juce::File (f)); break; }
        wave.repaint();
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::spaceKey) { playBtn.triggerClick(); return true; }
        return false;
    }

private:
    // ---------------------------------------------------------------- botões desenhados
    struct PlayButton : public juce::Button
    {
        BackingBar& o;
        explicit PlayButton (BackingBar& owner) : juce::Button ("play"), o (owner) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat().reduced (2.0f);
            const bool playing = o.bt.isPlaying();
            // brilho em volta quando está tocando
            if (playing)
            {
                g.setColour (ui::accent.withAlpha (0.18f));
                g.fillEllipse (r);
                r = r.reduced (3.0f);
            }
            juce::ColourGradient grad (ui::accent.brighter (over ? 0.35f : 0.2f), r.getCentreX(), r.getY(),
                                       ui::accent2.darker (down ? 0.3f : 0.05f), r.getCentreX(), r.getBottom(), false);
            g.setGradientFill (grad);
            g.fillEllipse (r);
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.drawEllipse (r.reduced (0.5f), 1.0f);
            g.setColour (juce::Colour (0xff17120a));
            const auto c = r.getCentre();
            const float s = r.getWidth() * 0.2f;
            if (playing)
            {
                g.fillRoundedRectangle (c.x - s * 0.95f, c.y - s, s * 0.7f, s * 2.0f, 1.5f);
                g.fillRoundedRectangle (c.x + s * 0.25f, c.y - s, s * 0.7f, s * 2.0f, 1.5f);
            }
            else
            {
                juce::Path t;
                t.addTriangle (c.x - s * 0.7f, c.y - s * 1.1f, c.x - s * 0.7f, c.y + s * 1.1f, c.x + s * 1.2f, c.y);
                g.fillPath (t);
            }
        }
    };

    struct IconButton : public juce::Button
    {
        enum Kind { Stop, Loop, Clear };
        BackingBar& o;
        Kind kind;
        IconButton (BackingBar& owner, Kind k) : juce::Button ("icon"), o (owner), kind (k) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            const bool on = kind == Loop && o.bt.getLoop();
            g.setColour (on ? ui::accent.withAlpha (down ? 0.32f : 0.22f) : juce::Colours::white.withAlpha (down ? 0.14f : over ? 0.09f : 0.04f));
            g.fillRoundedRectangle (r, 7.0f);
            g.setColour (on ? ui::accent.withAlpha (0.8f) : juce::Colours::white.withAlpha (0.16f));
            g.drawRoundedRectangle (r, 7.0f, 1.0f);
            const auto c = r.getCentre();
            const float s = r.getWidth() * 0.2f;
            g.setColour (on ? ui::accent : (over ? ui::text : juce::Colour (0xffcfcfd4)));
            if (kind == Stop)
                g.fillRoundedRectangle (c.x - s, c.y - s, 2 * s, 2 * s, 2.0f);
            else if (kind == Clear)
            {
                juce::Path x;
                x.startNewSubPath (c.x - s * 0.8f, c.y - s * 0.8f); x.lineTo (c.x + s * 0.8f, c.y + s * 0.8f);
                x.startNewSubPath (c.x + s * 0.8f, c.y - s * 0.8f); x.lineTo (c.x - s * 0.8f, c.y + s * 0.8f);
                g.strokePath (x, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            else
            {
                // setas em círculo (repetir)
                const float R = s * 1.25f;
                juce::Path a;
                a.addCentredArc (c.x, c.y, R, R * 0.8f, 0.0f, 0.35f, juce::MathConstants<float>::pi - 0.35f, true);
                a.addCentredArc (c.x, c.y, R, R * 0.8f, 0.0f, juce::MathConstants<float>::pi + 0.35f, juce::MathConstants<float>::twoPi - 0.35f, true);
                g.strokePath (a, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                auto head = [&] (float ang, float dir)
                {
                    const float px = c.x + R * std::sin (ang), py = c.y - R * 0.8f * std::cos (ang);
                    juce::Path h;
                    h.addTriangle (px, py - 3.5f, px, py + 3.5f, px + dir * 4.5f, py);
                    g.fillPath (h);
                };
                head (juce::MathConstants<float>::pi - 0.35f + 0.05f, -1.0f);
                head (juce::MathConstants<float>::twoPi - 0.35f + 0.05f, 1.0f);
            }
        }
    };

    struct CamToggle : public juce::Button
    {
        BackingBar& o;
        explicit CamToggle (BackingBar& owner) : juce::Button ("cam"), o (owner) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            const bool on = o.bt.sendToCam.load();
            const bool linked = o.proc.camLink.getStatus() == vlv::CamLink::Connected;
            g.setColour (on ? ui::accent.withAlpha (over ? 0.26f : 0.18f) : juce::Colours::white.withAlpha (over ? 0.08f : 0.04f));
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            g.setColour (on ? ui::accent.withAlpha (0.85f) : juce::Colours::white.withAlpha (0.18f));
            g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
            // câmera pequena
            auto ic = r.removeFromLeft (r.getHeight() + 6.0f).reduced (9.0f, 10.0f).translated (4.0f, 0.0f);
            const auto col = on ? ui::accent : ui::textDim;
            g.setColour (col);
            g.fillRoundedRectangle (ic.withWidth (ic.getWidth() * 0.72f), 2.0f);
            juce::Path lens;
            const float lx = ic.getX() + ic.getWidth() * 0.72f;
            lens.addTriangle (lx - 1.0f, ic.getCentreY(), ic.getRight() + 1.0f, ic.getY(), ic.getRight() + 1.0f, ic.getBottom());
            g.fillPath (lens);
            g.setFont (ui::Fonts::condensedBold (14.0f));
            g.setColour (on ? ui::text : ui::textDim);
            g.drawFittedText (on ? lang::t ("IN THE VIDEO", "NO VÍDEO") : lang::t ("NOT IN VIDEO", "FORA DO VÍDEO"),
                              r.toNearestInt().withTrimmedRight (8), juce::Justification::centred, 1, 0.8f);
            if (on && linked)
            {
                g.setColour (juce::Colour (0xff59c97a));
                g.fillEllipse (getWidth() - 12.0f, 5.0f, 6.0f, 6.0f);
            }
        }
    };

    // ---------------------------------------------------------------- forma de onda
    struct Wave : public juce::Component
    {
        BackingBar& o;
        explicit Wave (BackingBar& owner) : o (owner) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (juce::Colour (0xff09090b));
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (o.dragOver ? ui::accent : ui::line);
            g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, o.dragOver ? 2.0f : 1.0f);

            auto& th = o.bt.getThumbnail();
            const double len = o.bt.getLength();
            auto wr = r.reduced (12.0f, 10.0f).withTrimmedBottom (14.0f);
            if (! o.bt.hasFile() || len <= 0.0 || th.getTotalLength() <= 0.0)
            {
                g.setColour (o.dragOver ? ui::accent : ui::textDim);
                g.setFont (ui::Fonts::condensed (16.0f));
                g.drawFittedText (o.bt.hasFile() ? lang::t ("Reading the file...", "Lendo o arquivo...")
                                                 : lang::t ("Drag an audio file here (WAV, MP3, AIFF, FLAC, OGG) or click OPEN",
                                                            "Arraste um arquivo de áudio aqui (WAV, MP3, AIFF, FLAC, OGG) ou clique em ABRIR"),
                                  getLocalBounds().reduced (16, 0), juce::Justification::centred, 2);
                return;
            }
            const double pos = o.bt.getPosition();
            const float px = wr.getX() + (float) (juce::jlimit (0.0, 1.0, pos / len)) * wr.getWidth();
            // parte ainda não tocada
            g.setColour (ui::accent.withAlpha (0.28f));
            th.drawChannels (g, wr.toNearestInt(), 0.0, th.getTotalLength(), 1.0f);
            // parte já tocada
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (wr.withRight (px).toNearestInt());
                juce::ColourGradient grad (ui::accent.brighter (0.25f), wr.getX(), wr.getY(), ui::accent2, wr.getX(), wr.getBottom(), false);
                g.setGradientFill (grad);
                th.drawChannels (g, wr.toNearestInt(), 0.0, th.getTotalLength(), 1.0f);
            }
            // cursor
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.fillRect (px - 1.0f, wr.getY() - 4.0f, 2.0f, wr.getHeight() + 8.0f);
            g.setColour (ui::accent.withAlpha (0.25f));
            g.fillRect (px - 4.0f, wr.getY() - 4.0f, 8.0f, wr.getHeight() + 8.0f);

            // tempos
            g.setFont (ui::Fonts::condensed (13.0f));
            auto tl = r.reduced (12.0f, 4.0f).removeFromBottom (14.0f);
            g.setColour (ui::text);
            g.drawText (fmt (pos), tl, juce::Justification::centredLeft);
            g.setColour (ui::textDim);
            g.drawText (fmt (len), tl, juce::Justification::centredRight);
        }

        void mouseDown (const juce::MouseEvent& e) override { if (! o.bt.hasFile()) o.chooseFile(); else seek (e); }
        void mouseDrag (const juce::MouseEvent& e) override { if (o.bt.hasFile()) seek (e); }

        void seek (const juce::MouseEvent& e)
        {
            auto wr = getLocalBounds().toFloat().reduced (12.0f, 10.0f);
            const double frac = juce::jlimit (0.0, 1.0, (double) ((e.position.x - wr.getX()) / wr.getWidth()));
            o.bt.setPosition (frac * o.bt.getLength());
            repaint();
        }

        static juce::String fmt (double s)
        {
            const int t = (int) s;
            return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2);
        }
    };

    // ---------------------------------------------------------------- ações
    static bool isAudio (const juce::String& path)
    {
        const auto ext = juce::File (path).getFileExtension().toLowerCase();
        return ext == ".wav" || ext == ".mp3" || ext == ".aif" || ext == ".aiff" || ext == ".flac" || ext == ".ogg" || ext == ".m4a";
    }

    void chooseFile()
    {
        chooser = std::make_unique<juce::FileChooser> (lang::t ("Choose a backing track", "Escolha uma base"),
                                                       lastDir(), "*.wav;*.mp3;*.aif;*.aiff;*.flac;*.ogg;*.m4a");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto f = fc.getResult();
                                  if (f.existsAsFile()) loadFile (f);
                              });
    }

    juce::File lastDir() const
    {
        const auto f = bt.getFile();
        if (f.existsAsFile()) return f.getParentDirectory();
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    }

    void loadFile (const juce::File& f)
    {
        if (! bt.load (f))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Valved",
                lang::t ("This file could not be opened. Try a WAV or MP3.", "Não deu para abrir esse arquivo. Tente um WAV ou MP3."));
        refresh();
    }

    void refresh()
    {
        clearBtn.setVisible (bt.hasFile());
        stopBtn.setEnabled (bt.hasFile());
        repaint();
    }

    void timerCallback() override
    {
        if (bt.hasFile() != hadFile) { hadFile = bt.hasFile(); refresh(); }
        const bool playing = bt.isPlaying();
        if (playing || playing != wasPlaying) { wave.repaint(); playBtn.repaint(); }
        if (playing != wasPlaying) camBtn.repaint();
        wasPlaying = playing;
        const int st = (int) proc.camLink.getStatus();
        if (st != lastCam) { lastCam = st; camBtn.repaint(); }
        if (std::abs (vol.getValue() - bt.volumeDb.load()) > 0.05) vol.setValue (bt.volumeDb.load(), juce::dontSendNotification);
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { wave.repaint(); repaint(); }

    ValvedProcessor& proc;
    vlv::BackingTrack& bt;
    Wave wave;
    PlayButton playBtn { *this };
    IconButton stopBtn { *this, IconButton::Stop }, loopBtn { *this, IconButton::Loop };
    juce::TextButton openBtn { "OPEN" };
    IconButton clearBtn { *this, IconButton::Clear };
    CamToggle camBtn { *this };
    juce::Slider vol;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> titleArea, nameArea, volArea;
    bool dragOver = false, wasPlaying = false, hadFile = false;
    int lastCam = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BackingBar)
};

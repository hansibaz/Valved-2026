#pragma once
// Presets do Valved: 10 de fábrica (no código) + 50 espaços do usuário (arquivos).
// Os do usuário ficam em  Documentos/Valved/Presets/User NN.vpreset  (XML com o nome e todos os parâmetros),
// então dá para fazer backup ou trocar presets com outras pessoas copiando os arquivos.
// A qualidade (LOW/MED/HIGH) é do computador, não do timbre: não entra nos presets.
#include "PluginProcessor.h"

class PresetManager
{
public:
    static constexpr int NumFactory = 15, NumUser = 50;

    explicit PresetManager (ValvedProcessor& p) : proc (p) { takeSnapshot(); }

    static juce::File userDir()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Valved").getChildFile ("Presets");
    }
    static juce::File userFile (int slot) { return userDir().getChildFile ("User " + juce::String (slot + 1).paddedLeft ('0', 2) + ".vpreset"); }

    // ---------------------------------------------------------------- nomes
    static juce::StringArray factoryNames()
    {
        return { "Crystal Clean", "Ensemble Clean", "Brit Crunch", "Brit Lead", "Blues Breaker",
                 "51 Rhythm", "51 Lead", "Rec Modern", "DZ MOD Heavy", "Ambient Swell",
                 "Puppets Thrash", "Lightning 800", "Black Album", "Petrucci Rhythm", "Petrucci Lead" };
    }

    juce::String userName (int slot) const
    {
        const auto f = userFile (slot);
        if (! f.existsAsFile()) return {};
        if (auto xml = juce::XmlDocument::parse (f))
            return xml->getStringAttribute ("name", "User " + juce::String (slot + 1));
        return {};
    }
    bool userExists (int slot) const { return userFile (slot).existsAsFile(); }

    // índice global: 0..9 fábrica, 10..59 usuário, -1 nenhum
    int currentIndex() const { return proc.presetIndex; }
    juce::String currentName() const { return proc.presetName; }

    // ---------------------------------------------------------------- carregar
    bool load (int index)
    {
        if (index >= 0 && index < NumFactory) { applyFactory (index); setCurrent (index, factoryNames()[index]); return true; }
        const int slot = index - NumFactory;
        if (slot < 0 || slot >= NumUser) return false;
        auto xml = juce::XmlDocument::parse (userFile (slot));
        if (xml == nullptr || ! xml->hasTagName ("VALVEDPRESET")) return false;
        resetAll();
        for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
            set (e->getStringAttribute ("id"), (float) e->getDoubleAttribute ("value"));
        const auto ir = xml->getStringAttribute ("userIR");
        if (ir.isNotEmpty() && juce::File (ir).existsAsFile())
        {
            const int cab = (int) proc.apvts.getRawParameterValue (ParamID::cab)->load();
            proc.loadUserIR (juce::File (ir));
            set (ParamID::cab, (float) cab);   // carregar o IR não muda a caixa escolhida no preset
        }
        setCurrent (index, xml->getStringAttribute ("name"));
        return true;
    }

    // próximo / anterior (pula os espaços vazios)
    void step (int dir)
    {
        int i = proc.presetIndex;
        for (int n = 0; n < NumFactory + NumUser; ++n)
        {
            i = (i + dir + NumFactory + NumUser) % (NumFactory + NumUser);
            if (i < NumFactory || userExists (i - NumFactory)) { load (i); return; }
        }
    }

    // ---------------------------------------------------------------- salvar / apagar
    bool save (int slot, const juce::String& name)
    {
        juce::XmlElement xml ("VALVEDPRESET");
        xml.setAttribute ("name", name);
        xml.setAttribute ("version", 1);
        for (auto* prm : proc.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
            {
                if (rp->getParameterID() == ParamID::quality) continue;
                auto* e = xml.createNewChildElement ("PARAM");
                e->setAttribute ("id", rp->getParameterID());
                e->setAttribute ("value", rp->convertFrom0to1 (rp->getValue()));
            }
        const auto ir = proc.getUserIRFile();
        if (ir.existsAsFile()) xml.setAttribute ("userIR", ir.getFullPathName());
        userDir().createDirectory();
        if (! xml.writeTo (userFile (slot))) return false;
        setCurrent (NumFactory + slot, name);
        return true;
    }

    bool remove (int slot)
    {
        const bool ok = userFile (slot).deleteFile();
        if (proc.presetIndex == NumFactory + slot) { proc.presetIndex = -1; proc.presetName = {}; }
        return ok;
    }

    // ---------------------------------------------------------------- mudou depois de carregar?
    bool isModified() const
    {
        if (proc.presetIndex < 0) return false;
        const auto& ps = proc.getParameters();
        if ((int) snapshot.size() != ps.size()) return false;
        for (int i = 0; i < ps.size(); ++i)
            if (i != qualityIndex && std::abs (ps[i]->getValue() - snapshot[(size_t) i]) > 1.0e-4f) return true;
        return false;
    }
    void takeSnapshot()
    {
        snapshot.clear();
        const auto& ps = proc.getParameters();
        for (int i = 0; i < ps.size(); ++i)
        {
            snapshot.push_back (ps[i]->getValue());
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (ps[i]))
                if (rp->getParameterID() == ParamID::quality) qualityIndex = i;
        }
    }

private:
    void setCurrent (int index, const juce::String& name)
    {
        proc.presetIndex = index;
        proc.presetName = name;
        takeSnapshot();
    }

    void set (const juce::String& id, float v)
    {
        if (auto* p = proc.apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (v));
            p->endChangeGesture();
        }
    }

    // tudo volta ao padrão (menos a qualidade)
    void resetAll()
    {
        for (auto* prm : proc.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                if (rp->getParameterID() != ParamID::quality)
                {
                    rp->beginChangeGesture();
                    rp->setValueNotifyingHost (rp->getDefaultValue());
                    rp->endChangeGesture();
                }
    }

    void amp (int m, float gain, float bass, float mid, float treble, float presence, float master, float res = 5.0f)
    {
        set (ParamID::amp, (float) juce::jmin (m, 3));
        set (ParamID::dzMod, m == 4 ? 1.0f : 0.0f);
        set (ParamID::ampKnob (m, ParamID::gain), gain);
        set (ParamID::ampKnob (m, ParamID::bass), bass);
        set (ParamID::ampKnob (m, ParamID::mid), mid);
        set (ParamID::ampKnob (m, ParamID::treble), treble);
        set (ParamID::ampKnob (m, ParamID::presence), presence);
        set (ParamID::ampKnob (m, ParamID::master), master);
        set (ParamID::ampKnob (m, ParamID::resonance), res);
    }
    void cab (int c, int mic, float pos, float dist = 0.1f, float angle = 0.0f)
    {
        set (ParamID::cabOn, 1.0f); set (ParamID::cab, (float) c); set (ParamID::mic, (float) mic);
        set (ParamID::micX, pos); set (ParamID::micDist, dist); set (ParamID::micAngle, angle);
    }
    void pedal (int i, float drive, float tone, float level)
    {
        set (ParamID::pedal (i, "On"), 1.0f); set (ParamID::pedal (i, "Drive"), drive);
        set (ParamID::pedal (i, "Tone"), tone); set (ParamID::pedal (i, "Level"), level);
    }
    void delay (int type, float ms, float repeats, float mix, float tone = 6.0f, float mod = 2.0f)
    {
        set (ParamID::dlyOn, 1.0f); set (ParamID::dlyType, (float) type); set (ParamID::dlyTime, ms);
        set (ParamID::dlyRepeats, repeats); set (ParamID::dlyMix, mix); set (ParamID::dlyTone, tone); set (ParamID::dlyMod, mod);
    }
    void reverb (int type, float decay, float mix, float pre = 20.0f, float tone = 6.0f)
    {
        set (ParamID::revOn, 1.0f); set (ParamID::revType, (float) type); set (ParamID::revDecay, decay);
        set (ParamID::revMix, mix); set (ParamID::revPre, pre); set (ParamID::revTone, tone);
    }

    // Amps: 0 Clean 120, 1 Brit80, 2 51 Block, 3 Rec, 4 Rec DZ MOD.  Caixas: 0 Vintage, 1 Greener, 2 Twelve.
    // Mics: 0 SM57, 1 SM7B, 2 MD421.  Delay: 0 Digital, 1 Tape, 2 Analog... 7 Swell.  Reverb: 0 Room, 1 Hall, 2 Plate, 3 Spring, 6 Bloom, 7 Cloud.
    void applyFactory (int i)
    {
        resetAll();
        switch (i)
        {
            case 0: // Crystal Clean
                amp (0, 4.0f, 5.0f, 5.5f, 6.5f, 5.0f, 5.0f);
                cab (2, 0, 0.30f, 0.15f);
                reverb (1, 4.5f, 18.0f);
                break;
            case 1: // Ensemble Clean
                amp (0, 4.5f, 4.5f, 6.0f, 6.0f, 5.0f, 5.0f);
                cab (2, 2, 0.35f, 0.2f);
                set (ParamID::ceOn, 1.0f); set (ParamID::ceIntensity, 6.0f); set (ParamID::ceLevel, 5.0f);
                delay (0, 380.0f, 3.0f, 18.0f);
                reverb (2, 4.0f, 15.0f);
                break;
            case 2: // Brit Crunch
                amp (1, 6.0f, 5.0f, 6.5f, 6.0f, 5.5f, 5.0f);
                cab (1, 0, 0.25f);
                set (ParamID::gate, -72.0f);
                reverb (0, 3.5f, 12.0f);
                break;
            case 3: // Brit Lead
                amp (1, 8.0f, 5.0f, 6.5f, 6.0f, 6.0f, 5.0f);
                cab (1, 0, 0.22f);
                pedal (2, 3.0f, 5.5f, 4.5f);   // Gold Drive empurrando
                set (ParamID::gate, -68.0f);
                delay (1, 420.0f, 4.0f, 22.0f, 5.0f, 3.0f);
                reverb (2, 5.0f, 15.0f);
                break;
            case 4: // Blues Breaker
                amp (1, 4.5f, 5.5f, 6.0f, 5.5f, 5.0f, 5.0f);
                cab (0, 2, 0.35f, 0.15f, 10.0f);
                pedal (1, 5.0f, 5.5f, 6.4f);   // Yellow Drive
                reverb (3, 4.0f, 20.0f);
                break;
            case 5: // 51 Rhythm
                amp (2, 6.5f, 5.5f, 4.5f, 6.0f, 6.0f, 5.0f, 6.0f);
                cab (0, 0, 0.20f);
                set (ParamID::gate, -60.0f);
                break;
            case 6: // 51 Lead
                amp (2, 7.5f, 5.0f, 6.0f, 6.0f, 6.0f, 5.0f, 5.0f);
                cab (0, 0, 0.22f);
                pedal (0, 1.0f, 5.5f, 6.5f);
                set (ParamID::gate, -64.0f);
                delay (0, 450.0f, 4.0f, 22.0f);
                reverb (1, 5.0f, 15.0f);
                break;
            case 7: // Rec Modern
                amp (3, 7.0f, 6.0f, 4.0f, 6.5f, 6.0f, 5.0f);
                cab (0, 0, 0.18f);
                pedal (0, 0.0f, 6.0f, 8.0f);
                set (ParamID::gate, -56.0f);
                set (ParamID::eqOn, 1.0f); set (ParamID::eqMode, 1.0f);   // Mark: o "V" clássico
                set (ParamID::eqBand (1, 0), 3.0f); set (ParamID::eqBand (1, 2), -4.0f);
                set (ParamID::eqBand (1, 3), 1.0f); set (ParamID::eqBand (1, 4), 2.0f);
                break;
            case 8: // DZ MOD Heavy
                amp (4, 6.0f, 5.0f, 5.5f, 6.0f, 5.5f, 5.0f, 5.5f);
                set (ParamID::ampKnob (4, ParamID::powerAmp), 3.0f);
                cab (0, 0, 0.18f);
                pedal (0, 0.0f, 6.0f, 8.0f);
                set (ParamID::gate, -52.0f);
                break;
            case 9: // Ambient Swell
                amp (0, 4.0f, 5.0f, 5.0f, 6.0f, 5.0f, 5.0f);
                cab (2, 1, 0.40f, 0.3f);
                set (ParamID::ceOn, 1.0f); set (ParamID::ceIntensity, 5.0f);
                delay (7, 520.0f, 6.0f, 32.0f, 5.0f, 4.0f);
                reverb (6, 8.0f, 42.0f, 40.0f, 5.5f);
                break;
            // ---- Inspirados em timbres clássicos (configurações publicadas dos equipamentos; o resto se ajusta de ouvido)
            case 10: // Puppets Thrash: Mark IIC+ (gain 6, bass 6, mid 3, treble 7, presence 5) + EQ gráfico em "V"
                amp (3, 6.0f, 6.0f, 3.0f, 7.0f, 5.0f, 5.0f);
                cab (0, 0, 0.25f);
                set (ParamID::gate, -58.0f);
                set (ParamID::eqOn, 1.0f); set (ParamID::eqMode, 1.0f);
                set (ParamID::eqBand (1, 0), 4.0f); set (ParamID::eqBand (1, 1), 0.0f); set (ParamID::eqBand (1, 2), -5.0f);
                set (ParamID::eqBand (1, 3), 3.0f); set (ParamID::eqBand (1, 4), 0.0f);
                break;
            case 11: // Lightning 800: JCM800 (gain 6, bass 6, mid 3, treble 8, presence 6) empurrado por um TS808
                amp (1, 7.0f, 6.0f, 3.0f, 8.0f, 6.0f, 5.0f);
                cab (1, 0, 0.22f);
                pedal (0, 1.0f, 5.5f, 7.5f);
                set (ParamID::gate, -60.0f);
                reverb (0, 2.5f, 8.0f);
                break;
            case 12: // Black Album: Mark IIC+/IV (gain 6, bass 6, mid 4, treble 7, presence 6,5), "V" leve, sala curta
                amp (3, 6.0f, 6.0f, 4.0f, 7.0f, 6.5f, 5.0f);
                cab (0, 0, 0.30f, 0.15f);
                set (ParamID::gate, -60.0f);
                set (ParamID::eqOn, 1.0f); set (ParamID::eqMode, 1.0f);
                set (ParamID::eqBand (1, 0), 3.0f); set (ParamID::eqBand (1, 2), -3.0f); set (ParamID::eqBand (1, 3), 2.0f);
                reverb (0, 2.0f, 10.0f);
                break;
            case 13: // Petrucci Rhythm: Mark (gain 8, bass 5, mid 4, treble 6, presence 3) + EQ 80 +5 / 240 -4 / 750 -7 / 2200 +5 / 6600 +6
                amp (3, 8.0f, 5.0f, 4.0f, 6.0f, 3.0f, 5.0f);
                cab (0, 0, 0.22f);
                pedal (0, 0.0f, 6.0f, 7.0f);   // TS808 como boost
                set (ParamID::gate, -54.0f);
                set (ParamID::eqOn, 1.0f); set (ParamID::eqMode, 1.0f);
                set (ParamID::eqBand (1, 0), 5.0f); set (ParamID::eqBand (1, 1), -4.0f); set (ParamID::eqBand (1, 2), -7.0f);
                set (ParamID::eqBand (1, 3), 5.0f); set (ParamID::eqBand (1, 4), 6.0f);
                break;
            case 14: // Petrucci Lead: mais médio para o solo cantar, delay e hall
                amp (3, 8.5f, 5.0f, 5.5f, 6.0f, 4.0f, 5.0f);
                cab (0, 0, 0.25f);
                pedal (0, 0.0f, 6.0f, 7.0f);
                set (ParamID::gate, -60.0f);
                set (ParamID::eqOn, 1.0f); set (ParamID::eqMode, 1.0f);
                set (ParamID::eqBand (1, 0), 3.0f); set (ParamID::eqBand (1, 1), -2.0f); set (ParamID::eqBand (1, 2), -3.0f);
                set (ParamID::eqBand (1, 3), 4.0f); set (ParamID::eqBand (1, 4), 3.0f);
                delay (0, 480.0f, 4.0f, 22.0f, 5.5f, 2.0f);
                reverb (1, 4.5f, 16.0f);
                break;
            default: break;
        }
    }

    ValvedProcessor& proc;
    std::vector<float> snapshot;
    int qualityIndex = -1;
};

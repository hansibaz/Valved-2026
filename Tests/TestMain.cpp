// Teste offline: gera uma guitarra sintética (Karplus-Strong), passa pelo plugin
// em várias configurações, verifica níveis/NaN e grava WAVs; também salva um
// "print" da interface.
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

static juce::AudioBuffer<float> makeGuitar (double fs, double seconds)
{
    juce::AudioBuffer<float> b (1, (int) (fs * seconds));
    b.clear();
    juce::Random rng (42);
    // Acorde de Mi (power chord E2-B2-E3) seguido de palhetadas abafadas
    const double freqs[] = { 82.41, 123.47, 164.81 };
    auto pluck = [&] (int start, double f, float amp, float damp)
    {
        const int period = (int) (fs / f);
        std::vector<float> line ((size_t) period);
        for (auto& s : line) s = (rng.nextFloat() * 2 - 1) * amp;
        float last = 0;
        for (int i = start, k = 0; i < b.getNumSamples(); ++i, ++k)
        {
            auto& s = line[(size_t) (k % period)];
            const float out = s;
            s = damp * 0.5f * (s + last);
            last = out;
            b.addSample (0, i, out * 0.5f);
        }
    };
    for (int hit = 0; hit < 4; ++hit)
        for (int j = 0; j < 3; ++j)
            pluck ((int) (fs * (0.05 + hit * 0.9 + j * 0.008)), freqs[j], 0.35f, hit == 3 ? 0.998f : 0.9995f);
    return b;
}

static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double fs)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), fs,
        (unsigned) b.getNumChannels(), 24, {}, 0));
    if (w) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

static void setParam (ValvedProcessor& p, const juce::String& id, float plainValue)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plainValue));
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? argv[1] : "/tmp");
    const double fs = 48000.0;
    const int block = 256;
    auto guitar = makeGuitar (fs, 4.0);
    writeWav (outDir.getChildFile ("00_dry.wav"), guitar, fs);

    struct Case { const char* name; int amp; float gain; int transpose; bool chorus; int cab; float micX; float micDist; float reso = 5.0f; int recMode = 1; };
    const Case cases[] = {
        { "01_jazz120_clean_chorus", 0, 4.0f,  0, true,  2, 0.2f, 0.1f },
        { "02_brit410_crunch",       1, 5.0f,  0, false, 1, 0.3f, 0.1f },
        { "03_51block_highgain",     2, 7.0f,  0, false, 0, 0.25f, 0.1f },
        { "04_rec_highgain",       3, 7.0f,  0, false, 0, 0.25f, 0.1f },
        { "05_rec_drop_-2",        3, 7.0f, -2, false, 0, 0.25f, 0.1f },
        { "06_brit410_+5",           1, 5.0f,  5, false, 1, 0.3f, 0.1f },
        { "07_51block_mic_edge_far", 2, 7.0f,  0, false, 0, 0.95f, 0.8f },
        { "08_brit410_reso0",        1, 5.0f,  0, false, 1, 0.3f, 0.1f, 0.0f },
        { "09_brit410_reso10",       1, 5.0f,  0, false, 1, 0.3f, 0.1f, 10.0f },
        { "10_51block_reso10",       2, 7.0f,  0, false, 0, 0.25f, 0.1f, 10.0f },
        { "11_rec_vintage",          3, 7.0f,  0, false, 0, 0.25f, 0.1f, 5.0f, 0 },
        { "12_rec_modern",           3, 7.0f,  0, false, 0, 0.25f, 0.1f, 5.0f, 1 },
    };

    bool ok = true;
    const juce::String mode (argc > 2 ? argv[2] : "all");

    using Settings = std::vector<std::pair<juce::String, float>>;
    auto run = [&] (const juce::String& name, const Settings& settings, double tailSeconds)
    {
        ValvedProcessor p;
        p.setPlayConfigDetails (2, 2, fs, block);
        for (auto& [id, v] : settings) setParam (p, id, v);
        p.prepareToPlay (fs, block);
        juce::Thread::sleep (300); // a convolução carrega o IR em segundo plano

        const int total = guitar.getNumSamples() + (int) (tailSeconds * fs);
        juce::AudioBuffer<float> out (2, total);
        juce::MidiBuffer midi;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = juce::jmin (block, total - pos);
            juce::AudioBuffer<float> io (2, n);
            io.clear();
            if (pos < guitar.getNumSamples())
                io.copyFrom (0, 0, guitar, 0, pos, juce::jmin (n, guitar.getNumSamples() - pos));
            p.processBlock (io, midi);
            out.copyFrom (0, pos, io, 0, 0, n);
            out.copyFrom (1, pos, io, 1, 0, n);
            if (pos == block * 20) juce::Thread::sleep (100);
        }
        const double cpu = (juce::Time::getMillisecondCounterHiRes() - t0) / (1000.0 * total / fs) * 100.0;

        bool finite = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < out.getNumSamples(); ++i)
                if (! std::isfinite (out.getSample (ch, i))) finite = false;
        const float peak = juce::jmax (out.getMagnitude (0, 0, total), out.getMagnitude (1, 0, total));
        const float rms = out.getRMSLevel (0, 0, guitar.getNumSamples());
        const int tailStart = guitar.getNumSamples() + (int) (0.5 * fs);
        const float tail = tailSeconds > 0.6 ? out.getRMSLevel (0, tailStart, total - tailStart) : 0.0f;
        const float lrDiff = [&] { float d = 0; for (int i = 0; i < total; i += 7) d += std::abs (out.getSample (0, i) - out.getSample (1, i)); return d / (float) (total / 7); }();
        std::printf ("%-26s peak %6.1f  rms %6.1f  tail %6.1f  stereo %.4f  cpu %4.1f%%  %s\n", name.toRawUTF8(),
                     juce::Decibels::gainToDecibels (peak), juce::Decibels::gainToDecibels (rms),
                     juce::Decibels::gainToDecibels (tail, -120.0f), lrDiff, cpu, finite ? "ok" : "NaN!");
        ok = ok && finite && peak > 0.001f && peak < 4.0f;
        writeWav (outDir.getChildFile (name + ".wav"), out, fs);
    };

    if (mode == "all")
        for (auto& c : cases)
            run (c.name, { { ParamID::amp, (float) c.amp }, { ParamID::gain, c.gain }, { ParamID::transpose, (float) c.transpose },
                           { ParamID::chorus, c.chorus ? 1.0f : 0.0f }, { ParamID::cab, (float) c.cab }, { ParamID::micX, c.micX },
                           { ParamID::micDist, c.micDist }, { ParamID::resonance, c.reso }, { ParamID::recMode, (float) c.recMode } }, 0.0);

    if (mode == "all" || mode == "fx")
    {
        // Pedais: amp limpo, cada overdrive ligado
        const Settings cleanAmp { { ParamID::amp, 0.0f }, { ParamID::gain, 3.0f }, { ParamID::cab, 2.0f } };
        run ("20_clean_nopedal", cleanAmp, 0.0);
        for (int i = 0; i < 4; ++i)
        {
            auto st = cleanAmp;
            st.push_back ({ ParamID::pedal (i, "On"), 1.0f });
            st.push_back ({ ParamID::pedal (i, "Drive"), 6.0f });
            run ("2" + juce::String (i + 1) + "_clean_" + juce::String (vlv::pedalName (i)).replace (" ", "_").toLowerCase(), st, 0.0);
        }
        // Boost: Green com pouco drive e level alto na frente do Brit 410
        run ("25_brit_noboost", { { ParamID::amp, 1.0f }, { ParamID::gain, 5.0f }, { ParamID::cab, 1.0f } }, 0.0);
        run ("26_brit_greenboost", { { ParamID::amp, 1.0f }, { ParamID::gain, 5.0f }, { ParamID::cab, 1.0f },
                                     { ParamID::pedal (0, "On"), 1.0f }, { ParamID::pedal (0, "Drive"), 1.0f }, { ParamID::pedal (0, "Level"), 8.0f } }, 0.0);

        // Delay: todos os tipos
        for (int t = 0; t < vlv::StereoDelay::NumTypes; ++t)
        {
            auto st = cleanAmp;
            st.push_back ({ ParamID::dlyOn, 1.0f }); st.push_back ({ ParamID::dlyType, (float) t });
            st.push_back ({ ParamID::dlyTime, 380.0f }); st.push_back ({ ParamID::dlyRepeats, 6.0f }); st.push_back ({ ParamID::dlyMix, 40.0f });
            run ("3" + juce::String (t).paddedLeft ('0', 1) + "_delay_" + vlv::StereoDelay::typeNames()[t].replace ("-", "").toLowerCase(), st, 3.0);
        }
        // Delay no máximo de repetições (não pode disparar)
        run ("40_delay_tape_max", { { ParamID::amp, 0.0f }, { ParamID::dlyOn, 1.0f }, { ParamID::dlyType, 1.0f },
                                    { ParamID::dlyRepeats, 10.0f }, { ParamID::dlyMix, 50.0f } }, 4.0);

        // Reverb: todos os tipos
        for (int t = 0; t < vlv::AmbientReverb::NumTypes; ++t)
        {
            auto st = cleanAmp;
            st.push_back ({ ParamID::revOn, 1.0f }); st.push_back ({ ParamID::revType, (float) t });
            st.push_back ({ ParamID::revMix, 40.0f }); st.push_back ({ ParamID::revDecay, 5.0f });
            run ("5" + juce::String (t) + "_reverb_" + vlv::AmbientReverb::typeNames()[t].toLowerCase(), st, 4.0);
        }
        run ("60_reverb_cloud_max", { { ParamID::amp, 0.0f }, { ParamID::revOn, 1.0f }, { ParamID::revType, 6.0f },
                                      { ParamID::revDecay, 10.0f }, { ParamID::revMix, 100.0f }, { ParamID::revMod, 10.0f } }, 6.0);
        run ("61_reverb_shimmer_max", { { ParamID::amp, 0.0f }, { ParamID::revOn, 1.0f }, { ParamID::revType, 4.0f },
                                        { ParamID::revDecay, 10.0f }, { ParamID::revMix, 60.0f }, { ParamID::revParam, 10.0f } }, 6.0);
        run ("62_full_chain", { { ParamID::amp, 2.0f }, { ParamID::gain, 6.0f }, { ParamID::pedal (0, "On"), 1.0f },
                                { ParamID::pedal (0, "Drive"), 1.0f }, { ParamID::pedal (0, "Level"), 8.0f },
                                { ParamID::dlyOn, 1.0f }, { ParamID::dlyType, 2.0f }, { ParamID::revOn, 1.0f }, { ParamID::revType, 2.0f } }, 3.0);
    }

    if (mode == "all" || mode == "ui")
    // Prints da interface (um por amp + página do gabinete)
    {
        ValvedProcessor p;
        p.setPlayConfigDetails (2, 2, fs, block);
        p.prepareToPlay (fs, block);
        setParam (p, ParamID::transpose, -2.0f);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* ve = dynamic_cast<ValvedEditor*> (ed.get());
        ed->setVisible (true);
        auto shot = [&] (const juce::String& name)
        {
            ve->refreshNow();
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = outDir.getChildFile (name + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (img, os);
        };
        for (int a = 0; a < 4; ++a)
        {
            setParam (p, ParamID::amp, (float) a);
            setParam (p, ParamID::chorus, 1.0f);
            shot ("ui_amp" + juce::String (a));
            if (a == 3) { setParam (p, ParamID::recMode, 0.0f); shot ("ui_amp3_vintage"); setParam (p, ParamID::recMode, 1.0f); }
        }
        setParam (p, ParamID::micX, 0.45f);
        setParam (p, ParamID::micDist, 0.2f);
        ve->showPage (ValvedEditor::PageCab);
        shot ("ui_cab");
        setParam (p, ParamID::pedal (0, "On"), 1.0f);
        setParam (p, ParamID::pedal (2, "On"), 1.0f);
        ve->showPage (ValvedEditor::PagePedals);
        shot ("ui_pedals");
        setParam (p, ParamID::dlyOn, 1.0f);
        setParam (p, ParamID::dlyType, 1.0f);
        setParam (p, ParamID::revOn, 1.0f);
        setParam (p, ParamID::revType, 4.0f);
        ve->showPage (ValvedEditor::PageFx);
        shot ("ui_fx");
        setParam (p, ParamID::dlySync, 1.0f);
        setParam (p, ParamID::revOn, 0.0f);
        shot ("ui_fx_sync");
    }

    std::printf (ok ? "ALL OK\n" : "FAILED\n");
    return ok ? 0 : 1;
}

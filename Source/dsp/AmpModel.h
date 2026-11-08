#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include "Biquad.h"
#include "TubeAmp.h"
#include "Overdrive.h"

namespace vlv
{
// Todos os amps usam o mesmo motor de circuito (TubeAmp.h). Cada um tem o seu circuito,
// com valores dos esquemas e calibrados por medições dos amps reais:
// 0: Clean 120 (JC-120)   1: Brit80 (JCM800 2203)   2: 51 Block (5150 Lead)   3: Rec (Dual Rectifier)
inline const std::array<const char*, 4>& ampNames()
{
    static const std::array<const char*, 4> n { { "Clean 120", "Brit80", "51 Block", "Rec" } };
    return n;
}

class AmpModel
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        oversampling->initProcessing ((size_t) juce::jmax (1, maxBlock));
        osFs = fs * 4.0;
        const tube::CircuitSpec specs[NumCircuits] = { tube::jcClean(), tube::brit80s(), tube::block5150(), tube::recRectifier() };
        for (int i = 0; i < NumCircuits; ++i)
        {
            auto sp = specs[i];
            sp.outTrimDb += circuitTrimDb[i];
            tube::applyOverrides (sp, "c" + std::to_string (i) + ".");
            circuits[i].prepare (osFs, fs);
            circuits[i].configure (sp);
        }
        dirty = true;
        reset();
    }

    void reset()
    {
        for (auto& c : circuits) c.reset();
        if (oversampling) oversampling->reset();
    }

    struct Settings
    {
        int model = 1;
        float gain = 5, bass = 5, mid = 5, treble = 5, presence = 5, master = 5, resonance = 5;
        bool operator!= (const Settings& o) const
        {
            return model != o.model || gain != o.gain || bass != o.bass || mid != o.mid || treble != o.treble // NOLINT
                || presence != o.presence || master != o.master || resonance != o.resonance;
        }
    };

    void setParameters (const Settings& s)
    {
        Settings n = s;
        n.model = juce::jlimit (0, 3, n.model);
        const bool circuitChange = n.model != set.model;
        if (n != set) { set = n; dirty = true; }
        if (circuitChange) reset();
    }

    void process (float* data, int numSamples)
    {
        if (dirty)
        {
            dirty = false;
            circuits[currentCircuit()].setKnobs (set.gain, set.bass, set.mid, set.treble, set.presence, set.master, set.resonance);
        }
        float* chans[1] = { data };
        juce::dsp::AudioBlock<float> block (chans, 1, (size_t) numSamples);
        auto up = oversampling->processSamplesUp (block);
        circuits[currentCircuit()].processOS (up.getChannelPointer (0), (int) up.getNumSamples());
        oversampling->processSamplesDown (block);
    }

    enum { CircClean = 0, CircBrit80, CircBlock, CircRec, NumCircuits };
    // Nivelamento de volume entre os amps (medido com os knobs no meio)
    static constexpr float circuitTrimDb[NumCircuits] = { 10.1f, -5.6f, -0.1f, -13.0f }; // nivelamento v0.9 (LUFS com DI, knobs padrão)

private:
    int currentCircuit() const
    {
        switch (set.model)
        {
            case 0:  return CircClean;
            case 1:  return CircBrit80;
            case 2:  return CircBlock;
            default: return CircRec;
        }
    }

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    double fs = 48000, osFs = 192000;
    Settings set;
    bool dirty = true;
    tube::CircuitAmp circuits[NumCircuits];
};

// Chorus Ensemble no estilo Boss CE-1 (o mesmo circuito do chorus do JC-120):
//  - BBD de 512 estágios (MN3002) com atraso base de ~2,5 ms
//  - LFO triangular; no CE-1 o knob Intensity aumenta profundidade E velocidade (~1 Hz a ~3 Hz,
//    ~1,75 Hz no meio)
//  - Caminho molhado escuro: filtros anti-aliasing/reconstrução do BBD (~8 kHz), queda de ~3 dB/oitava
//    a partir de 1 kHz e leve realce de médios-graves
//  - Saídas estéreo como no JC-120: um lado direto (seco), o outro só efeito; em mono, 50/50
class CE1Chorus
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        buf.assign (4096, 0.0f);
        w = 0; lfo = 0.25;
        preLp.lowpass (fs, 8000.0, 0.707);  postLp.lowpass (fs, 8000.0, 0.707);
        postLp2.lowpass (fs, 9000.0, 0.54);
        tilt.setCutoff (fs, 1000.0);
        lowMid.peak (fs, 220.0, 0.8, 1.5);
        preLp.reset(); postLp.reset(); postLp2.reset(); tilt.reset(); lowMid.reset();
    }

    // intensity: 0..1 (knob DEPTH)   rateTrim: 0..1 (knob RATE; 0,5 = igual ao CE-1)
    void setControls (float intensity, float rateTrim)
    {
        intens = juce::jlimit (0.0f, 1.0f, intensity);
        const double mult = std::pow (2.0, (juce::jlimit (0.0f, 1.0f, rateTrim) - 0.5) * 2.0);
        rateHz = 1.0 * std::pow (3.0, (double) intens) * mult;
        widthMs = 0.6 + 3.4 * (double) intens;
    }

    inline void process (float x, float& direct, float& effect) noexcept
    {
        // Entrada do BBD (filtro anti-aliasing)
        buf[(size_t) w] = preLp.process (x);
        lfo += rateHz / fs;
        if (lfo >= 1.0) lfo -= 1.0;
        const double tri = lfo < 0.5 ? 4.0 * lfo - 1.0 : 3.0 - 4.0 * lfo;      // -1..1 triangular
        const double delayMs = baseMs + 0.5 * widthMs * (tri + 1.0);
        const double d = delayMs * 0.001 * fs;
        const double pos = (double) w - d;
        const double fl = std::floor (pos);
        const float t = (float) (pos - fl);
        const int i = (int) fl;
        auto at = [this] (int k) { return buf[(size_t) (k & 4095)]; };
        const float ym1 = at (i - 1), y0 = at (i), y1 = at (i + 1), y2 = at (i + 2);
        const float c1 = 0.5f * (y1 - ym1), c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2, c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        float wet = ((c3 * t + c2) * t + c1) * t + y0;
        w = (w + 1) & 4095;

        // Reconstrução do BBD + caráter escuro do CE-1
        wet = postLp2.process (postLp.process (wet));
        wet = 0.5f * wet + 0.5f * tilt.lp (wet);          // ~ -3 dB/oitava acima de 1 kHz
        wet = lowMid.process (wet);
        wet = std::tanh (wet * 1.3f) / 1.3f;               // o CE-1 satura fácil
        direct = x;
        effect = wet * 1.12f;
    }

private:
    std::vector<float> buf;
    double fs = 48000, lfo = 0.25, rateHz = 1.73, widthMs = 2.3;
    const double baseMs = 2.5;
    float intens = 0.5f;
    int w = 0;
    Biquad preLp, postLp, postLp2, lowMid;
    OnePole tilt;
};
} // namespace vlv

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
    // quality: 0 baixa, 1 média, 2 alta (ver osOrderFor)
    void prepare (double sampleRate, int maxBlock, int quality = 1)
    {
        fs = sampleRate;
        osOrder = osOrderFor (quality, fs);
        // Alta: filtros FIR de fase linear (os mais limpos, com mais atraso); baixa/média: IIR (sem atraso)
        const auto ftype = quality >= 2 ? juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple
                                        : juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR;
        oversampling.reset();
        if (osOrder > 0)
        {
            oversampling = std::make_unique<juce::dsp::Oversampling<float>> (1, (size_t) osOrder, ftype, quality >= 2, quality >= 2);
            oversampling->initProcessing ((size_t) juce::jmax (1, maxBlock));
        }
        osFs = fs * (double) (1 << osOrder);
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
        if (oversampling == nullptr) { circuits[currentCircuit()].processOS (data, numSamples); return; }
        float* chans[1] = { data };
        juce::dsp::AudioBlock<float> block (chans, 1, (size_t) numSamples);
        auto up = oversampling->processSamplesUp (block);
        circuits[currentCircuit()].processOS (up.getChannelPointer (0), (int) up.getNumSamples());
        oversampling->processSamplesDown (block);
    }

    // atraso (em amostras da DAW) dos filtros de sobreamostragem
    int getLatencySamples() const { return oversampling ? (int) std::lround (oversampling->getLatencyInSamples()) : 0; }

    // Sobreamostragem: a taxa interna do circuito fica perto de 96 kHz (baixa), 192 kHz (média)
    // ou 384 kHz (alta), qualquer que seja a taxa do projeto.
    static int osOrderFor (int quality, double fs)
    {
        const double target[3] = { 96000.0, 192000.0, 384000.0 };
        const int q = juce::jlimit (0, 2, quality);
        int order = (int) std::lround (std::log2 (target[q] / fs));
        return juce::jlimit (q == 0 ? 0 : 1, 4, order);
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
    int osOrder = 2;
    double fs = 48000, osFs = 192000;
    Settings set;
    bool dirty = true;
    tube::CircuitAmp circuits[NumCircuits];
};

} // namespace vlv

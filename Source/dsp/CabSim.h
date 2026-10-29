#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Biquad.h"

namespace vlv
{
// Gabinetes internos gerados por filtros (sem arquivos externos) e simulação de posição
// de microfone. Também carrega IRs (respostas ao impulso) do usuário.
enum CabType { Cab4x12Vintage = 0, Cab4x12Brit, Cab2x12Open, CabUserIR };

inline juce::StringArray getCabNames()
{
    return { "4x12 Vintage", "4x12 British", "2x12 Open Back", "User IR" };
}

// micX: 0 = centro do cone (brilhante) .. 1 = borda (escuro)
// micDist: 0 = colado no alto-falante (mais graves) .. 1 = afastado (mais ambiente)
inline juce::AudioBuffer<float> makeCabIR (int type, float micX, float micDist, double fs)
{
    const int n = (int) (fs * 0.12);
    juce::AudioBuffer<float> ir (1, n);
    ir.clear();
    float* d = ir.getWritePointer (0);

    // Impulso direto + reflexão do chão/sala que aparece com a distância
    d[0] = 1.0f;
    const int refl = (int) ((0.4 + 2.6 * micDist) * 0.001 * fs);
    if (refl > 0 && refl < n) d[refl] += 0.10f + 0.30f * micDist;
    const int refl2 = (int) ((1.3 + 5.0 * micDist) * 0.001 * fs);
    if (refl2 > 0 && refl2 < n) d[refl2] -= 0.15f * micDist;

    struct Spec { float hpF, hpQ, lowBumpF, lowBumpDb, lp, pk1F, pk1Db, pk2F, pk2Db, dipF, dipDb; };
    static const Spec specs[3] = {
        //  hp    Q    bump  dB    lp     pk1    dB   pk2    dB   dip   dB
        {  75, 1.10f, 115, 3.5f, 6200, 2500, 5.0f, 4400, 3.0f, 1500, -3.0f }, // V30: médios-agudos agressivos
        {  80, 1.00f, 105, 3.0f, 5200, 1900, 3.0f, 3300, 2.0f,  900, -1.5f }, // Greenback: mais suave e quente
        {  90, 0.70f, 150, 1.0f, 8500, 3000, 3.0f, 5500, 2.0f,  450, -2.5f }, // 2x12 aberto: graves soltos, agudos abertos
    };
    const Spec& s = specs[juce::jlimit (0, 2, type)];

    const float lpCut = s.lp * (1.0f - 0.55f * micX) * (1.0f - 0.15f * micDist);
    const float presScale = 1.0f - 0.75f * micX;

    Biquad f[10];
    f[0].highpass (fs, s.hpF, s.hpQ);
    f[1].peak (fs, s.lowBumpF, 1.2, s.lowBumpDb);
    f[2].lowShelf (fs, 220.0, 5.0 * (1.0 - micDist) - 2.0);       // efeito de proximidade
    f[3].peak (fs, s.dipF, 1.4, s.dipDb);
    f[4].peak (fs, s.pk1F, 1.6, s.pk1Db * presScale);
    f[5].peak (fs, s.pk2F, 2.0, s.pk2Db * presScale);
    f[6].peak (fs, 320.0, 1.0, 2.5 * micX);                       // borda: mais "corpo"
    f[7].highShelf (fs, 6000.0, -4.0 * micDist);                  // longe: perde brilho
    f[8].lowpass (fs, lpCut, 0.9);                                // queda natural do falante
    f[9].lowpass (fs, lpCut * 1.15, 0.6);

    for (int i = 0; i < n; ++i)
    {
        float x = d[i];
        for (auto& b : f) x = b.process (x);
        d[i] = x;
    }

    // Fade-out suave no final
    const int fade = n / 4;
    for (int i = 0; i < fade; ++i)
        d[n - fade + i] *= 1.0f - (float) i / (float) fade;

    return ir;
}

class CabSim
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        conv.prepare (spec);
        sampleRate = spec.sampleRate;
    }

    void reset() { conv.reset(); }

    void loadBuiltIn (int type, float micX, float micDist)
    {
        auto ir = makeCabIR (type, micX, micDist, sampleRate);
        conv.loadImpulseResponse (std::move (ir), sampleRate,
                                  juce::dsp::Convolution::Stereo::no,
                                  juce::dsp::Convolution::Trim::no,
                                  juce::dsp::Convolution::Normalise::yes);
    }

    void loadFile (const juce::File& file)
    {
        conv.loadImpulseResponse (file,
                                  juce::dsp::Convolution::Stereo::no,
                                  juce::dsp::Convolution::Trim::yes,
                                  (size_t) (sampleRate * 2.0),   // até 2 segundos
                                  juce::dsp::Convolution::Normalise::yes);
    }

    void process (juce::dsp::AudioBlock<float>& block)
    {
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        conv.process (ctx);
        block.multiplyBy (makeup);   // compensa a perda de volume da normalização do IR
    }

    double getSampleRate() const { return sampleRate; }

private:
    juce::dsp::Convolution conv;
    double sampleRate = 48000;
    float makeup = 4.0f;   // +12 dB
};
} // namespace vlv

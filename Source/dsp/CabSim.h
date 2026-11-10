#pragma once
#include <juce_dsp/juce_dsp.h>
#include "CabSpeakers.h"

namespace vlv
{
// Gabinetes 4x12 internos, cada um com a curva oficial do seu alto-falante Celestion
// (ver CabSpeakers.h), mais simulação de posição de microfone. Também carrega IRs do usuário.
enum CabType { Cab4x12Vintage = 0, Cab4x12Greener, Cab4x12Twelve, CabUserIR };

inline juce::StringArray getCabNames()
{
    return { "Vintage", "Greener", "Twelve", "User IR" };
}

// micX: 0 = centro do cone (brilhante) .. 1 = borda (escuro)
// micDist: 0 = colado no alto-falante (mais graves) .. 1 = afastado (mais ambiente)
// Na posição padrão do microfone (0,25 / 0,1) a resposta é exatamente a da ficha técnica.
inline juce::AudioBuffer<float> makeCabIR (int type, float micX, float micDist, double fs)
{
    const int n = (int) (fs * 0.12);
    juce::AudioBuffer<float> ir (1, n);
    ir.clear();
    float* d = ir.getWritePointer (0);

    const int model = type == Cab4x12Greener ? speakers::Greenback
                    : type == Cab4x12Twelve  ? speakers::G12T75
                                             : speakers::Vintage30;
    auto v = speakers::makeIR (model, fs, n, micX, micDist);
    for (int i = 0; i < n; ++i) d[i] = v[(size_t) i];

    // reflexões da sala só quando o microfone se afasta da posição padrão
    const float far = juce::jmax (0.0f, (micDist - 0.1f) / 0.9f);
    if (far > 0.0f)
    {
        const int r1 = (int) ((0.4 + 2.6 * micDist) * 0.001 * fs), r2 = (int) ((1.3 + 5.0 * micDist) * 0.001 * fs);
        for (int i = 0; i < n; ++i)
        {
            if (i >= r1) d[i] += (0.40f * far) * v[(size_t) (i - r1)];
            if (i >= r2) d[i] -= (0.15f * far) * v[(size_t) (i - r2)];
        }
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
        // Volume absoluto: mantém a diferença de sensibilidade entre os falantes (100 / 98 / 97 dB)
        // e a mudança de volume ao mover o microfone. Referência: V30 com o mic na posição padrão.
        if (refScaleRate != sampleRate)
        {
            auto ref = makeCabIR (Cab4x12Vintage, 0.25f, 0.1f, sampleRate);
            const float* r = ref.getReadPointer (0);
            double e = 0.0;
            for (int i = 0; i < ref.getNumSamples(); ++i) e += (double) r[i] * r[i];
            refScale = e > 1e-12 ? (float) (0.125 / std::sqrt (e)) : 1.0f;
            refScaleRate = sampleRate;
        }
        ir.applyGain (refScale);
        conv.loadImpulseResponse (std::move (ir), sampleRate,
                                  juce::dsp::Convolution::Stereo::no,
                                  juce::dsp::Convolution::Trim::no,
                                  juce::dsp::Convolution::Normalise::no);
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
    double refScaleRate = 0.0;
    float refScale = 1.0f;
    float makeup = 4.0f;   // +12 dB
};
} // namespace vlv

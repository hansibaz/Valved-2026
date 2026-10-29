#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Biquad.h"

namespace vlv
{
// Filtro de 1 polo (passa-baixa; passa-alta = x - passa-baixa)
struct OnePole
{
    float a = 0.0f, z = 0.0f;
    void setCutoff (double fs, double f) { a = (float) std::exp (-2.0 * kPi * juce::jlimit (5.0, fs * 0.45, f) / fs); }
    void reset() { z = 0.0f; }
    inline float lp (float x) noexcept { z = x + a * (z - x); return z; }
    inline float hp (float x) noexcept { return x - lp (x); }
};

// Quatro overdrives clássicos (nomes e desenhos próprios):
//  Green  — inspirado no TS808/TS9: "mid hump", clipagem suave simétrica, sinal limpo somado
//  Yellow — inspirado no SD-1: clipagem assimétrica, mais ganho e mais brilho
//  Gold   — inspirado no Klon: mistura de limpo + sujo, quase transparente, médios encorpados
//  Blue   — inspirado no King of Tone: aberto, graves cheios, saturação de amp
enum PedalType { PedalGreen = 0, PedalYellow, PedalGold, PedalBlue, NumPedals };

class Overdrive
{
public:
    explicit Overdrive (PedalType t = PedalGreen) : type (t) {}
    void setType (PedalType t) { type = t; dirty = true; }

    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        os = std::make_unique<juce::dsp::Oversampling<float>> (1, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        os->initProcessing ((size_t) juce::jmax (1, maxBlock));
        osFs = fs * 2.0;
        dc.prepare (osFs, 10.0);
        dryCopy.assign ((size_t) juce::jmax (1, maxBlock), 0.0f);
        dirty = true;
        reset();
    }

    void reset()
    {
        hpIn.reset(); hpIn2.reset(); preLp.reset(); toneLp.reset(); postLp.reset();
        midPeak.reset(); shelf.reset(); dc.reset();
        if (os) os->reset();
    }

    // drive/tone/level: 0..10
    void setParams (bool on, float drive, float tone, float level)
    {
        enabled = on;
        if (drive != pDrive || tone != pTone || level != pLevel) // NOLINT
        {
            pDrive = drive; pTone = tone; pLevel = level;
            dirty = true;
        }
    }

    bool isActive() const { return enabled || mix > 0.0f; }

    void process (float* data, int n)
    {
        const float target = enabled ? 1.0f : 0.0f;
        if (! enabled && mix <= 0.0f) return;           // desligado: bypass real
        if (enabled && mix <= 0.0f) reset();            // ao ligar, começa limpo
        if (dirty) update();

        if ((int) dryCopy.size() < n) dryCopy.resize ((size_t) n);
        std::copy (data, data + n, dryCopy.begin());

        float* chans[1] = { data };
        juce::dsp::AudioBlock<float> block (chans, 1, (size_t) n);
        auto up = os->processSamplesUp (block);
        float* u = up.getChannelPointer (0);
        const int nu = (int) up.getNumSamples();
        for (int i = 0; i < nu; ++i)
            u[i] = shape (u[i]);
        os->processSamplesDown (block);

        const float step = 1.0f / (0.01f * (float) fs);
        for (int i = 0; i < n; ++i)
        {
            float y = data[i];
            y = toneStage (y);
            y *= levelGain;
            if (mix < target) mix = juce::jmin (target, mix + step);
            else if (mix > target) mix = juce::jmax (target, mix - step);
            data[i] = dryCopy[(size_t) i] + (y - dryCopy[(size_t) i]) * mix;
        }
    }

private:
    void update()
    {
        dirty = false;
        const float d = juce::jlimit (0.0f, 1.0f, pDrive / 10.0f);
        const float t = juce::jlimit (0.0f, 1.0f, pTone / 10.0f);
        const float l = juce::jlimit (0.0f, 1.0f, pLevel / 10.0f);

        switch (type)
        {
            case PedalGreen:
                hpIn.setCutoff (osFs, 720.0);                  // o famoso "mid hump"
                preLp.setCutoff (osFs, 6500.0);
                gain = 4.0f * std::pow (30.0f, d);             // ~12 dB a ~42 dB
                toneLp.setCutoff (fs, 600.0 * std::pow (12.0, t));
                postLp.setCutoff (fs, 9000.0);
                levelGain = dbToGain (-32.0f + 38.0f * l);
                break;
            case PedalYellow:
                hpIn.setCutoff (osFs, 560.0);
                preLp.setCutoff (osFs, 8000.0);
                gain = 5.0f * std::pow (45.0f, d);
                toneLp.setCutoff (fs, 900.0 * std::pow (10.0, t));
                postLp.setCutoff (fs, 11000.0);
                levelGain = dbToGain (-33.0f + 38.0f * l);
                break;
            case PedalGold:
                hpIn.setCutoff (osFs, 380.0);
                hpIn2.setCutoff (osFs, 90.0);
                preLp.setCutoff (osFs, 5200.0);
                midPeak.peak (osFs, 1000.0, 0.8, 3.0);
                gain = 2.0f * std::pow (25.0f, d);
                cleanAmt = 1.0f - 0.8f * d;                    // potenciômetro duplo: mais ganho, menos limpo
                dirtyAmt = 0.25f + 0.9f * d;
                shelf.highShelf (fs, 1800.0, (t - 0.5f) * 18.0f);
                postLp.setCutoff (fs, 12000.0);
                levelGain = dbToGain (-26.0f + 34.0f * l);
                break;
            case PedalBlue:
            default:
                hpIn.setCutoff (osFs, 240.0);
                preLp.setCutoff (osFs, 7000.0);
                gain = 2.0f * std::pow (40.0f, d);
                cleanAmt = 0.35f * (1.0f - d);
                shelf.highShelf (fs, 2500.0, (t - 0.5f) * 16.0f);
                postLp.setCutoff (fs, 9500.0);
                levelGain = dbToGain (-30.0f + 36.0f * l);
                break;
        }
    }

    inline float shape (float x) noexcept
    {
        switch (type)
        {
            case PedalGreen:
            {
                // Clean + (estágio com diodos no laço de realimentação, só nos médios)
                const float v = preLp.lp (hpIn.hp (x)) * gain;
                return x + 0.55f * std::tanh (v / 0.55f);
            }
            case PedalYellow:
            {
                const float v = preLp.lp (hpIn.hp (x)) * gain;
                const float c = v > 0.0f ? 0.62f * std::tanh (v / 0.62f) : 0.42f * std::tanh (v / 0.42f);
                return dc.process (x + c);
            }
            case PedalGold:
            {
                const float body = hpIn2.hp (x);
                float v = midPeak.process (preLp.lp (hpIn.hp (x))) * gain;
                v = v / std::pow (1.0f + std::pow (std::abs (v), 3.0f), 1.0f / 3.0f) * 0.5f;   // germânio, mais "duro"
                return cleanAmt * body + dirtyAmt * v;
            }
            case PedalBlue:
            default:
            {
                const float v = preLp.lp (hpIn.hp (x)) * gain;
                const float soft = std::tanh (v);
                const float hard = juce::jlimit (-0.8f, 0.8f, v);
                return cleanAmt * x + 0.55f * (0.65f * soft + 0.35f * hard);
            }
        }
    }

    inline float toneStage (float x) noexcept
    {
        switch (type)
        {
            case PedalGreen:
            case PedalYellow: return postLp.lp (toneLp.lp (x));
            case PedalGold:
            case PedalBlue:
            default:          return postLp.lp (shelf.process (x));
        }
    }

    PedalType type;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    double fs = 48000, osFs = 96000;
    bool enabled = false, dirty = true;
    float mix = 0.0f;
    float pDrive = 5, pTone = 5, pLevel = 5;
    float gain = 1, levelGain = 1, cleanAmt = 0, dirtyAmt = 1;
    OnePole hpIn, hpIn2, preLp, toneLp, postLp;
    Biquad midPeak, shelf;
    DCBlocker dc;
    std::vector<float> dryCopy;
};

inline const char* pedalName (int t)
{
    static const char* names[] = { "GREEN DRIVE", "YELLOW DRIVE", "GOLD DRIVE", "BLUE DRIVE" };
    return names[juce::jlimit (0, 3, t)];
}
} // namespace vlv

#pragma once
#include <cmath>
#include <algorithm>

namespace vlv
{
// Noise gate rápido, no estilo dos gates dos plugins "Neural" (fecha seco, sem sobra de chiado):
//  - detector RMS curto (3 ms), que acompanha o fim da nota sem atraso;
//  - histerese: abre no limiar, fecha 4 dB abaixo (não fica "batendo" no limiar);
//  - hold curto (25 ms) para não cortar a nota entre uma palhetada e outra;
//  - fechamento em dB por milissegundo (curva logarítmica, como o ouvido percebe):
//    -90 dB em ~45 ms, sem a cauda longa de um fechamento exponencial;
//  - abertura em 0,3 ms (a palhetada passa inteira).
class NoiseGate
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        detCoef    = (float) std::exp (-1.0 / (0.003 * fs));
        openCoef   = (float) std::exp (-1.0 / (0.0003 * fs));
        closeDbPerSample = (float) (90.0 / (0.045 * fs));
        holdSamples = (int) (0.025 * fs);
        reset();
    }

    void reset() { power = 0; gain = 1; gainDb = 0; holdCount = 0; isOpen = true; }

    void setThresholdDb (float db)
    {
        bypass = db <= -89.0f;
        openThr  = std::pow (10.0f, db / 20.0f);
        closeThr = openThr * 0.63f;   // fecha 4 dB abaixo de onde abre
    }

    inline float process (float x) noexcept
    {
        if (bypass) { gain = 1.0f; gainDb = 0.0f; return x; }

        power = x * x + detCoef * (power - x * x);
        const float env = std::sqrt (power);

        if (env > openThr) { isOpen = true; holdCount = holdSamples; }
        else if (env < closeThr)
        {
            if (holdCount > 0) --holdCount;
            else isOpen = false;
        }

        if (isOpen)
        {
            gain = 1.0f + openCoef * (gain - 1.0f);
            gainDb = 20.0f * std::log10 (std::max (gain, 1.0e-5f));
        }
        else
        {
            gainDb = std::max (-100.0f, gainDb - closeDbPerSample);
            gain = gainDb <= -99.0f ? 0.0f : std::pow (10.0f, gainDb * 0.05f);
        }
        return x * gain;
    }

    // Calcula o ganho a partir do sinal-chave (guitarra limpa), sem processar áudio
    inline float keyGain (float key) noexcept { process (key); return gain; }

    float getGain() const { return gain; }

private:
    double fs = 48000;
    float power = 0, gain = 1, gainDb = 0, detCoef = 0.99f, openCoef = 0.9f, closeDbPerSample = 0.04f;
    float openThr = 0.001f, closeThr = 0.0005f;
    int holdSamples = 1200, holdCount = 0;
    bool isOpen = true, bypass = true;
};
} // namespace vlv

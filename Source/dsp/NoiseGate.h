#pragma once
#include <cmath>
#include <algorithm>

namespace vlv
{
// Noise gate com histerese (abre no limiar, fecha 6 dB abaixo) e hold.
class NoiseGate
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        detRelease = (float) std::exp (-1.0 / (0.010 * fs));   // detector RMS: média de ~10 ms
        openCoef   = (float) std::exp (-1.0 / (0.0005 * fs));  // abre em 0,5 ms
        closeCoef  = (float) std::exp (-1.0 / (0.080 * fs));   // fecha em 80 ms (não corta o fim das notas)
        holdSamples = (int) (0.060 * fs);
        reset();
    }

    void reset() { env = 0; power = 0; gain = 1; holdCount = 0; isOpen = true; }

    void setThresholdDb (float db)
    {
        bypass = db <= -89.0f;
        openThr  = std::pow (10.0f, db / 20.0f);
        closeThr = openThr * 0.7f;   // fecha 3 dB abaixo de onde abre
    }

    inline float process (float x) noexcept
    {
        if (bypass) { gain = 1.0f; return x; }

        // Detector de energia (RMS): o chiado tem picos altos mas energia baixa; a nota tem energia alta
        power = x * x + detRelease * (power - x * x);
        env = std::sqrt (power);

        if (env > openThr) { isOpen = true; holdCount = holdSamples; }
        else if (env < closeThr)
        {
            if (holdCount > 0) --holdCount;
            else isOpen = false;
        }

        const float target = isOpen ? 1.0f : 0.0f;
        const float c = isOpen ? openCoef : closeCoef;
        gain = target + c * (gain - target);
        return x * gain;
    }

    // Calcula o ganho a partir do sinal-chave (guitarra limpa), sem processar áudio
    inline float keyGain (float key) noexcept { process (key); return gain; }

    float getGain() const { return gain; }

private:
    double fs = 48000;
    float power = 0, env = 0, gain = 1, detRelease = 0.99f, openCoef = 0.9f, closeCoef = 0.999f;
    float openThr = 0.001f, closeThr = 0.0005f;
    int holdSamples = 2000, holdCount = 0;
    bool isOpen = true, bypass = true;
};
} // namespace vlv

#pragma once
#include <vector>
#include <cmath>
#include "Biquad.h"

namespace vlv
{
// Pitch shifter de baixa latência (duas cabeças de leitura em linha de atraso,
// com crossfade). Polifônico: funciona com acordes. Latência média ~ metade da janela.
// v0.1: versão simples; no futuro pode ser trocado por um algoritmo de maior qualidade.
class PitchShifter
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        windowLen = 0.040 * fs;   // janela de 40 ms (cobre bem notas graves de afinações baixas)
        buffer.assign (size, 0.0f);
        writePos = 0;
        phase = 0;
        wet = (semis != 0) ? 1.0f : 0.0f;
        wetCoef = (float) std::exp (-1.0 / (0.015 * fs));
    }

    void setSemitones (int s)
    {
        semis = s;
        ratio = std::pow (2.0, s / 12.0);
    }

    inline float process (float x) noexcept
    {
        buffer[(size_t) writePos] = x;

        phase += (1.0 - ratio) / windowLen;
        phase -= std::floor (phase);

        const double ph2 = phase + 0.5 - std::floor (phase + 0.5);
        const double d1 = minDelay + phase * windowLen;
        const double d2 = minDelay + ph2 * windowLen;

        const double s1 = std::sin (kPi * phase);
        const double g1 = s1 * s1;            // g1 + g2 = 1
        const double g2 = 1.0 - g1;

        const float shifted = (float) (g1 * read (d1) + g2 * read (d2));

        writePos = (writePos + 1) & mask;

        const float wetTarget = (semis != 0) ? 1.0f : 0.0f;
        wet = wetTarget + wetCoef * (wet - wetTarget);
        if (wet < 1.0e-4f && semis == 0) return x;
        return x * (1.0f - wet) + shifted * wet;
    }

    int getSemitones() const { return semis; }

private:
    // Leitura com interpolação cúbica (Hermite)
    inline float read (double delay) const noexcept
    {
        const double pos = (double) writePos - delay;
        const double fl = std::floor (pos);
        const float t = (float) (pos - fl);
        const int i = (int) fl;
        auto at = [this] (int k) { return buffer[(size_t) (k & mask)]; };
        const float ym1 = at (i - 1), y0 = at (i), y1 = at (i + 1), y2 = at (i + 2);
        const float c0 = y0;
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }

    static constexpr int size = 1 << 16;
    static constexpr int mask = size - 1;
    static constexpr double minDelay = 3.0;

    std::vector<float> buffer;
    double fs = 48000, windowLen = 1920, phase = 0, ratio = 1.0;
    int writePos = 0, semis = 0;
    float wet = 0, wetCoef = 0.999f;
};
} // namespace vlv

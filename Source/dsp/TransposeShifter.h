#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include "Biquad.h"

namespace vlv
{
// Transpose para guitarra (polifônico, baixa latência).
//
// Um "cabeçote de leitura" percorre o áudio mais devagar (afinação abaixo) ou mais rápido
// (acima) que a gravação. De tempos em tempos ele precisa saltar para não se afastar demais.
// O problema do método antigo era saltar em pontos fixos: a forma de onda antiga e a nova
// ficavam fora de fase e se cancelavam ("tremolo").
// Aqui, antes de cada salto, o algoritmo procura (por correlação) o ponto em que a forma de
// onda nova se encaixa na antiga — como um editor que corta exatamente no ciclo da nota —
// e faz o crossfade entre trechos já alinhados. Resultado: volume estável, afinação exata.
class TransposeShifter
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        buf.assign ((size_t) size, 0.0f);
        w = 0; readPos = 0; fromPos = 0; xfLeft = 0;
        corrLen   = (int) (0.010 * fs);   // janela de comparação (10 ms)
        search    = (int) (0.026 * fs);   // procura em ~2 ciclos do Mi grave
        xfLen     = (int) (0.012 * fs);   // crossfade entre trechos alinhados
        minJump   = (int) (0.004 * fs);
        wet = semis != 0 ? 1.0f : 0.0f;
        wetCoef = (float) std::exp (-1.0 / (0.010 * fs));
        resetPointer();
    }

    void setSemitones (int s)
    {
        if (s == semis) return;
        semis = s;
        ratio = std::pow (2.0, s / 12.0);
        resetPointer();
    }

    int getSemitones() const { return semis; }

    // Atraso médio (para compensação de latência na DAW)
    int getLatencySamples() const
    {
        if (semis == 0) return 0;
        return ratio < 1.0 ? corrLen + search / 2 : xfLen + minJump + search / 2;
    }

    inline float process (float x) noexcept
    {
        buf[(size_t) (w & mask)] = x;

        float y = x;
        if (semis != 0 || wet > 1.0e-4f)
        {
            const double delay = (double) w - readPos;
            if (xfLeft <= 0)
            {
                if (ratio < 1.0 && delay > (double) (corrLen + search + 2)) jump (false);
                else if (ratio > 1.0 && delay < (double) xfLen * ratio + 4.0) jump (true);
            }

            float s = read (readPos);
            if (xfLeft > 0)
            {
                const float t = 1.0f - (float) xfLeft / (float) xfLen;
                const float g = 0.5f - 0.5f * std::cos (3.14159265f * t);
                s = read (fromPos) * (1.0f - g) + s * g;
                fromPos += ratio;
                --xfLeft;
            }
            readPos += ratio;

            const float target = semis != 0 ? 1.0f : 0.0f;
            wet = target + wetCoef * (wet - target);
            y = x + (s - x) * wet;
        }
        ++w;
        if (w >= wrapAt) { w -= wrapShift; readPos -= wrapShift; fromPos -= wrapShift; }
        return y;
    }

private:
    void resetPointer()
    {
        const double d = ratio < 1.0 ? (double) corrLen + 2.0 : (double) (xfLen + minJump + search);
        readPos = (double) w - d;
        xfLeft = 0;
    }

    // Procura o salto J que melhor alinha a forma de onda e inicia o crossfade
    void jump (bool backward)
    {
        const std::int64_t ro = (std::int64_t) std::floor (readPos);
        const std::int64_t delay = w - ro;
        std::int64_t jLo, jHi;
        if (! backward) { jHi = delay - corrLen - 1; jLo = std::max<std::int64_t> (minJump, jHi - search); }
        else            { jLo = minJump; jHi = minJump + search; }
        if (jHi <= jLo) { jLo = std::max<std::int64_t> (1, jHi - 1); }

        double best = -2.0; std::int64_t bestJ = jHi;
        double ea = 0.0;
        for (int k = 0; k < corrLen; k += 2) { const float a = at (ro + k); ea += (double) a * a; }
        for (std::int64_t j = jLo; j <= jHi; ++j)
        {
            const std::int64_t cand = backward ? ro - j : ro + j;
            double ab = 0.0, eb = 0.0;
            for (int k = 0; k < corrLen; k += 2)
            {
                const float a = at (ro + k), b = at (cand + k);
                ab += (double) a * b; eb += (double) b * b;
            }
            const double c = (ea > 1e-12 && eb > 1e-12) ? ab / std::sqrt (ea * eb) : 0.0;
            if (c > best) { best = c; bestJ = j; }
        }
        fromPos = readPos;
        readPos = backward ? readPos - (double) bestJ : readPos + (double) bestJ;
        xfLeft = xfLen;
    }

    inline float at (std::int64_t i) const noexcept { return buf[(size_t) (i & mask)]; }

    // Interpolação cúbica (Hermite)
    inline float read (double pos) const noexcept
    {
        const double fl = std::floor (pos);
        const float t = (float) (pos - fl);
        const std::int64_t i = (std::int64_t) fl;
        const float ym1 = at (i - 1), y0 = at (i), y1 = at (i + 1), y2 = at (i + 2);
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * t + c2) * t + c1) * t + y0;
    }

    static constexpr std::int64_t size = 1 << 16, mask = size - 1;
    static constexpr std::int64_t wrapAt = std::int64_t (1) << 40, wrapShift = std::int64_t (1) << 39;
    std::vector<float> buf;
    double fs = 48000, ratio = 1.0, readPos = 0, fromPos = 0;
    std::int64_t w = 0;
    int semis = 0, corrLen = 480, search = 1250, xfLen = 576, minJump = 192, xfLeft = 0;
    float wet = 0.0f, wetCoef = 0.999f;
};
} // namespace vlv

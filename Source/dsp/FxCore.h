#pragma once
// Blocos básicos de alta qualidade para os efeitos de tempo (delays e reverbs).
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <memory>
#include <cstring>
#include <juce_dsp/juce_dsp.h>
#include "Biquad.h"

namespace vlv::fx
{
inline int nextPow2 (int v) { int p = 1; while (p < v) p <<= 1; return p; }

// Linha de atraso com leitura fracionária por interpolação cúbica (Hermite de 4 pontos):
// modulação sem perda de agudos e sem o "zumbido" da interpolação linear.
struct DelayLine
{
    std::vector<float> buf; int mask = 0, w = 0;
    void init (int maxSamples) { buf.assign ((size_t) nextPow2 (std::max (8, maxSamples + 8)), 0.0f); mask = (int) buf.size() - 1; w = 0; }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    inline void push (float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }
    // atraso inteiro (1 = amostra escrita por último)
    inline float tap (int d) const noexcept { return buf[(size_t) ((w - d) & mask)]; }
    inline float read (double d) const noexcept
    {
        const double pos = (double) w - d;
        const double fl = std::floor (pos);
        const float t = (float) (pos - fl);
        const int i = (int) fl;
        const float ym1 = buf[(size_t) ((i - 1) & mask)], y0 = buf[(size_t) (i & mask)];
        const float y1 = buf[(size_t) ((i + 1) & mask)], y2 = buf[(size_t) ((i + 2) & mask)];
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * t + c2) * t + c1) * t + y0;
    }
};

// Gerador de números aleatórios rápido (xorshift)
struct Rng
{
    uint32_t s = 0x9E3779B9u;
    explicit Rng (uint32_t seed = 1) : s (seed * 2654435761u + 1u) {}
    inline float uni() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s & 0xFFFFFF) / 16777216.0f; }   // 0..1
    inline float bi() noexcept { return 2.0f * uni() - 1.0f; }                                                                 // -1..1
};

// LFO "orgânico": caminho aleatório suave (interpolação cosseno entre pontos sorteados).
// Soa natural em reverbs e chorus de fita, sem o vai-e-vem mecânico de um seno puro.
struct SmoothRandom
{
    Rng rng; double phase = 0, inc = 0; float a = 0, b = 0;
    explicit SmoothRandom (uint32_t seed = 7) : rng (seed) { a = rng.bi(); b = rng.bi(); }
    void setRate (double fs, double hz) { inc = hz / fs; }
    inline float next() noexcept
    {
        phase += inc;
        if (phase >= 1.0) { phase -= 1.0; a = b; b = rng.bi(); }
        const float t = (float) (0.5 - 0.5 * std::cos (kPi * phase));
        return a + (b - a) * t;
    }
};

// Senoide em quadratura barata (oscilador de rotação)
struct SineLfo
{
    double c = 1, s = 0, cr = 1, sr = 0;
    void setRate (double fs, double hz) { cr = std::cos (2.0 * kPi * hz / fs); sr = std::sin (2.0 * kPi * hz / fs); }
    inline float next() noexcept
    {
        const double nc = c * cr - s * sr, ns = s * cr + c * sr;
        const double k = 1.5 - 0.5 * (nc * nc + ns * ns);   // mantém a amplitude 1
        c = nc * k; s = ns * k;
        return (float) s;
    }
};

// Allpass de Schroeder com atraso (opcionalmente modulado)
struct Allpass
{
    DelayLine d; double len = 100; float g = 0.6f;
    void init (int maxLen, double l, float gain) { d.init (maxLen + 64); len = l; g = gain; }
    void clear() { d.clear(); }
    inline float process (float x, double mod = 0.0) noexcept
    {
        const float del = mod == 0.0 ? d.tap ((int) len) : d.read (len + mod);
        const float v = x + g * del;
        d.push (v);
        return del - g * v;
    }
};

// Filtro de decaimento de 2 bandas (absorção de Jot): ganho gLo nos graves e gHi nos agudos,
// transição suave em fc. Faz cada banda decair no tempo certo (agudos morrem antes, como numa sala).
struct DecayFilter
{
    float gLo = 0.9f, gHi = 0.8f, a = 0.5f, z = 0.0f;
    void set (double fs, double fc, float lo, float hi) { gLo = lo; gHi = hi; a = (float) std::exp (-2.0 * kPi * fc / fs); }
    void reset() { z = 0.0f; }
    inline float process (float x) noexcept
    {
        z = x + a * (z - x);                 // passa-baixas
        return gHi * x + (gLo - gHi) * z;    // agudos com gHi, graves com gLo
    }
};

// Transformada de Hadamard rápida (16 pontos), normalizada (matriz ortogonal)
inline void hadamard16 (float* x) noexcept
{
    for (int h = 1; h < 16; h <<= 1)
        for (int i = 0; i < 16; i += h << 1)
            for (int j = i; j < i + h; ++j)
            {
                const float a = x[j], b = x[j + h];
                x[j] = a + b; x[j + h] = a - b;
            }
    for (int i = 0; i < 16; ++i) x[i] *= 0.25f;
}

// Saturação suave (só age em níveis altos; protege laços de realimentação)
inline float softClip (float x) noexcept
{
    if (x > 1.5f) return 1.0f;
    if (x < -1.5f) return -1.0f;
    return x - (4.0f / 27.0f) * x * x * x;
}

// ---------------------------------------------------------------------------------
// Pitch shifter espectral (phase vocoder) para o shimmer: cada parcial do som é movido para a
// nova frequência com a fase corrigida, então acordes e caudas de reverb sobem de tom limpos,
// sem o "batimento" dos shifters de cabeças/grãos. Janela de 4096 amostras com 75% de
// sobreposição. A latência (~85 ms) não importa dentro do reverb.
// ---------------------------------------------------------------------------------
class SpectralShifter
{
public:
    static constexpr int order = 12, N = 1 << order, osamp = 4, step = N / osamp, latency = N - step;

    void prepare (double sampleRate, double ratio)
    {
        fs = sampleRate; r = ratio;
        // compensação de volume medida (o mapeamento dos bins espalha a energia do parcial)
        makeup = std::abs (r - 1.0) < 1e-3 ? 1.0f : (std::abs (r - 2.0) < 1e-3 ? 1.71f : 2.38f);
        fft = std::make_unique<juce::dsp::FFT> (order);
        win.resize ((size_t) N);
        for (int k = 0; k < N; ++k) win[(size_t) k] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * k / N));
        inFifo.assign ((size_t) N, 0.0f); outFifo.assign ((size_t) N, 0.0f);
        work.assign ((size_t) (2 * N), 0.0f); accum.assign ((size_t) (2 * N), 0.0f);
        lastPhase.assign ((size_t) (N / 2 + 1), 0.0f); sumPhase.assign ((size_t) (N / 2 + 1), 0.0f);
        anaMag.assign ((size_t) (N / 2 + 1), 0.0f); anaFrq.assign ((size_t) (N / 2 + 1), 0.0f);
        synMag.assign ((size_t) (N / 2 + 1), 0.0f); synFrq.assign ((size_t) (N / 2 + 1), 0.0f);
        rover = latency;
    }
    void clear()
    {
        for (auto* v : { &inFifo, &outFifo, &work, &accum, &lastPhase, &sumPhase }) std::fill (v->begin(), v->end(), 0.0f);
        rover = latency;
    }

    inline float process (float x) noexcept
    {
        inFifo[(size_t) rover] = x;
        const float y = outFifo[(size_t) (rover - latency)];
        if (++rover >= N) { rover = latency; frame(); }
        return y;
    }

private:
    void frame() noexcept
    {
        const double expct = 2.0 * kPi * step / N;
        std::fill (work.begin(), work.end(), 0.0f);
        for (int k = 0; k < N; ++k) work[(size_t) k] = inFifo[(size_t) k] * win[(size_t) k];
        fft->performRealOnlyForwardTransform (work.data(), true);

        // análise: magnitude e frequência "verdadeira" de cada bin
        for (int k = 0; k <= N / 2; ++k)
        {
            const float re = work[(size_t) (2 * k)], im = work[(size_t) (2 * k + 1)];
            const float mag = std::sqrt (re * re + im * im);
            const float ph = std::atan2 (im, re);
            double d = ph - lastPhase[(size_t) k];
            lastPhase[(size_t) k] = ph;
            d -= k * expct;
            d -= 2.0 * kPi * std::floor (d / (2.0 * kPi) + 0.5);       // volta para -pi..pi
            anaMag[(size_t) k] = mag;
            anaFrq[(size_t) k] = (float) (k + d * osamp / (2.0 * kPi)); // em bins (fracionário)
        }
        // transposição: cada bin vai para k * r
        std::fill (synMag.begin(), synMag.end(), 0.0f);
        std::fill (synFrq.begin(), synFrq.end(), 0.0f);
        for (int k = 0; k <= N / 2; ++k)
        {
            const int j = (int) std::lround (k * r);
            if (j > N / 2) break;
            if (anaMag[(size_t) k] > synMag[(size_t) j]) synFrq[(size_t) j] = (float) (anaFrq[(size_t) k] * r);
            synMag[(size_t) j] += anaMag[(size_t) k];
        }
        // síntese: fase acumulada pela frequência nova
        for (int k = 0; k <= N / 2; ++k)
        {
            const double d = (synFrq[(size_t) k] - k) * 2.0 * kPi / osamp + k * expct;
            sumPhase[(size_t) k] = (float) std::fmod (sumPhase[(size_t) k] + d, 2.0 * kPi);
            work[(size_t) (2 * k)] = synMag[(size_t) k] * std::cos (sumPhase[(size_t) k]);
            work[(size_t) (2 * k + 1)] = synMag[(size_t) k] * std::sin (sumPhase[(size_t) k]);
        }
        for (int k = N / 2 + 1; k < N; ++k) { work[(size_t) (2 * k)] = 0.0f; work[(size_t) (2 * k + 1)] = 0.0f; }
        fft->performRealOnlyInverseTransform (work.data());
        // janela de síntese + soma com sobreposição (Hann² com 75% soma 1,5)
        for (int k = 0; k < N; ++k) accum[(size_t) k] += work[(size_t) k] * win[(size_t) k] * (makeup / 1.5f);
        for (int k = 0; k < step; ++k) outFifo[(size_t) k] = accum[(size_t) k];
        std::memmove (accum.data(), accum.data() + step, sizeof (float) * (size_t) N);
        std::memmove (inFifo.data(), inFifo.data() + step, sizeof (float) * (size_t) latency);
    }

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> win, inFifo, outFifo, work, accum, lastPhase, sumPhase, anaMag, anaFrq, synMag, synFrq;
    double fs = 48000, r = 2.0;
    float makeup = 1.0f;
    int rover = latency;
};
} // namespace vlv::fx

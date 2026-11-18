#pragma once
// Pedal de EQ gráfico (fica depois do amp e da caixa, antes do delay e do reverb).
// Dois modos:
//  - VALVED (7 bandas): faixas escolhidas para guitarra distorcida/limpa:
//      80 Hz (prateleira: peso/"boom")      200 Hz (corpo / embolado)
//      400 Hz (som de "caixa de papelão")   800 Hz (médio "honk", presença na banda)
//      1,6 kHz (ataque / definição)         3,2 kHz (mordida)
//      6,4 kHz (prateleira: brilho / chiado)
//    Q proporcional: com pouco ajuste a banda é larga e musical; com muito, fica mais focada
//    (corta o problema sem levar o resto junto).
//  - MARK (5 bandas): o EQ gráfico da série Mark — 80, 240, 750, 2200 e 6600 Hz, ±12 dB,
//    bandas largas (~1,4 oitava) como o EQ de giradores do amp. O clássico é o "V": 750 Hz para
//    baixo, pontas para cima.
#include <array>
#include <cmath>
#include "Biquad.h"

namespace vlv
{
class GraphicEq
{
public:
    static constexpr int MaxBands = 7;
    static constexpr int ValvedBands = 7, MarkBands = 5;
    static const double* freqs (int mode)
    {
        static const double v[7] = { 80.0, 200.0, 400.0, 800.0, 1600.0, 3200.0, 6400.0 };
        static const double m[5] = { 80.0, 240.0, 750.0, 2200.0, 6600.0 };
        return mode == 1 ? m : v;
    }
    static int numBands (int mode) { return mode == 1 ? MarkBands : ValvedBands; }

    struct Settings
    {
        bool on = false;
        int mode = 0;                    // 0 VALVED, 1 MARK
        std::array<float, MaxBands> gainDb {};
        float levelDb = 0.0f;
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& ch : f) for (auto& b : ch) b.reset();
        last = {}; last.mode = -1;
        mix = 0.0f; step = (float) (1.0 / (0.015 * fs));
    }

    void process (float* L, float* R, int n, const Settings& s)
    {
        const float target = s.on ? 1.0f : 0.0f;
        if (! s.on && mix <= 0.0f) return;
        if (changed (s)) design (s);
        const float lvl = std::pow (10.0f, s.levelDb / 20.0f);
        const int nb = numBands (s.mode);
        for (int c = 0; c < 2; ++c)
        {
            float* x = c == 0 ? L : R;
            if (x == nullptr) continue;
            float m = mix;
            for (int i = 0; i < n; ++i)
            {
                float y = x[i];
                for (int b = 0; b < nb; ++b) y = f[(size_t) c][(size_t) b].process (y);
                y *= lvl;
                if (m != target) m = target > m ? std::min (target, m + step) : std::max (target, m - step);
                x[i] += (y - x[i]) * m;
            }
            if (c == 1 || R == nullptr) mix = m;
        }
    }

private:
    bool changed (const Settings& s) const
    {
        if (s.mode != last.mode) return true;
        for (int b = 0; b < MaxBands; ++b) if (std::abs (s.gainDb[(size_t) b] - last.gainDb[(size_t) b]) > 0.01f) return true;
        return false;
    }
    void design (const Settings& s)
    {
        const double* fr = freqs (s.mode);
        const int nb = numBands (s.mode);
        if (s.mode != last.mode) for (auto& ch : f) for (auto& b : ch) b.reset();
        for (int b = 0; b < nb; ++b)
        {
            const double g = s.gainDb[(size_t) b];
            const double fc = std::min (fr[b], 0.45 * fs);
            for (int c = 0; c < 2; ++c)
            {
                auto& bq = f[(size_t) c][(size_t) b];
                if (s.mode == 1)
                    bq.peak (fs, fc, 1.0, g);                                  // Mark: bandas largas (Q ~1)
                else if (b == 0)
                    bq.lowShelf (fs, fc * 1.6, g);                             // prateleira de grave
                else if (b == nb - 1)
                    bq.highShelf (fs, fc * 0.8, g);                            // prateleira de agudo
                else
                    bq.peak (fs, fc, 0.9 + 0.08 * std::abs (g), g);            // Q proporcional ao ajuste
            }
        }
        last = s;
    }

    double fs = 48000;
    std::array<std::array<Biquad, MaxBands>, 2> f;
    Settings last;
    float mix = 0.0f, step = 0.001f;
};
} // namespace vlv

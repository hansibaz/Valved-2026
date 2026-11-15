#pragma once
// Pedal de chorus "Ensemble", seguindo o circuito do Boss CE-1 Chorus Ensemble (o mesmo chorus
// do Roland JC-120):
//  - Pré-amplificador de entrada com o knob LEVEL (o CE-1 é famoso por esse pré: com o LEVEL
//    alto ele "esquenta" e satura de leve, deixando o som mais gordo).
//  - Linha de atraso BBD de 512 estágios (MN3002) com atraso base de ~2,5 ms, com os filtros
//    anti-aliasing / reconstrução (~8 kHz) e o tom escuro característico do CE-1.
//  - LFO triangular.
//  - Modo CHORUS: o knob INTENSITY aumenta a profundidade E a velocidade ao mesmo tempo
//    (~1 Hz a ~3 Hz), e a saída mono mistura 50/50 o sinal direto com o atrasado — é essa
//    mistura que cria o "chorus" (as duas cópias desafinando uma contra a outra).
//  - Modo VIBRATO: só o sinal atrasado (modulação de afinação pura), com DEPTH e RATE próprios.
//  - Mono, porque fica no começo da cadeia, antes de todos os pedais e do amp.
#include <vector>
#include <cmath>
#include <algorithm>
#include "Biquad.h"
#include "Overdrive.h"
#include <juce_core/juce_core.h>

namespace vlv::pedal
{
class EnsembleChorus
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        int sz = 1; while (sz < (int) (fs * 0.03)) sz <<= 1;     // até 30 ms
        buf.assign ((size_t) sz, 0.0f); mask = sz - 1; w = 0;
        lfo = 0.25;
        preLp.lowpass (fs, 8000.0, 0.707);
        postLp.lowpass (fs, 8000.0, 0.707);
        postLp2.lowpass (fs, 9000.0, 0.54);
        tilt.setCutoff (fs, 1000.0);
        lowMid.peak (fs, 220.0, 0.8, 1.5);
        inHp.highpass (fs, 20.0, 0.707);
        preLp.reset(); postLp.reset(); postLp2.reset(); tilt.reset(); lowMid.reset(); inHp.reset();
        onGain = on ? 1.0f : 0.0f;
        fadeStep = (float) (1.0 / (0.015 * fs));
    }

    // level/intensity/depth/rate: 0..10 (knobs);  vibrato: false = CHORUS, true = VIBRATO
    void setParams (bool enabled, bool vibrato, float level, float intensity, float depth, float rate)
    {
        on = enabled; vib = vibrato;
        // pré: LEVEL 5 = ganho unitário; 0 = -12 dB; 10 = +14 dB na entrada do pré
        const float lv = juce::jlimit (0.0f, 10.0f, level);
        inGain = std::pow (10.0f, (lv <= 5.0f ? (lv - 5.0f) * 2.4f : (lv - 5.0f) * 2.8f) / 20.0f);
        outComp = 1.0f / std::sqrt (inGain);                       // sobe o volume pela metade (como no pedal)
        if (! vib)
        {
            const double in = juce::jlimit (0.0, 1.0, intensity / 10.0);
            rateHz = 1.0 * std::pow (3.0, in);                       // ~1 Hz a ~3 Hz (1,73 Hz no meio)
            widthMs = 0.6 + 3.4 * in;                                // varredura do atraso
        }
        else
        {
            const double dp = juce::jlimit (0.0, 1.0, depth / 10.0), rt = juce::jlimit (0.0, 1.0, rate / 10.0);
            rateHz = 0.5 * std::pow (16.0, rt);                      // 0,5 Hz a 8 Hz
            widthMs = 0.3 + 5.2 * dp;
        }
    }

    void process (float* x, int n) noexcept
    {
        const float target = on ? 1.0f : 0.0f;
        if (! on && onGain <= 0.0f)
        {
            // desligado: mantém a linha de atraso andando (sem estalo ao ligar)
            for (int i = 0; i < n; ++i) { buf[(size_t) w] = 0.0f; w = (w + 1) & mask; }
            return;
        }
        for (int i = 0; i < n; ++i)
        {
            const float dry = x[i];
            // Pré-amplificador de entrada (saturação suave, levemente assimétrica)
            const float v = inHp.process (dry) * guitarVolts * inGain;
            const float pre = (sat * std::tanh (v / sat) + 0.04f * v * v / (1.0f + v * v)) / guitarVolts;

            // BBD
            buf[(size_t) w] = preLp.process (pre);
            lfo += rateHz / fs;
            if (lfo >= 1.0) lfo -= 1.0;
            const double tri = lfo < 0.5 ? 4.0 * lfo - 1.0 : 3.0 - 4.0 * lfo;
            const double d = (baseMs + 0.5 * widthMs * (tri + 1.0)) * 0.001 * fs;
            float wet = read (d);
            w = (w + 1) & mask;
            wet = postLp2.process (postLp.process (wet));
            wet = 0.5f * wet + 0.5f * tilt.lp (wet);       // ~ -3 dB/oitava acima de 1 kHz
            wet = lowMid.process (wet);
            wet *= 1.12f;

            const float fx = (vib ? wet * 1.25f : 0.62f * (pre + wet) * 1.3f) * outComp;

            if (onGain != target) onGain = target > onGain ? std::min (target, onGain + fadeStep) : std::max (target, onGain - fadeStep);
            x[i] = dry + (fx - dry) * onGain;
        }
    }

private:
    inline float read (double d) const noexcept
    {
        const double pos = (double) w - d;
        const double fl = std::floor (pos);
        const float t = (float) (pos - fl);
        const int i = (int) fl;
        const float ym1 = buf[(size_t) ((i - 1) & mask)], y0 = buf[(size_t) (i & mask)];
        const float y1 = buf[(size_t) ((i + 1) & mask)], y2 = buf[(size_t) ((i + 2) & mask)];
        const float c1 = 0.5f * (y1 - ym1), c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2, c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * t + c2) * t + c1) * t + y0;
    }

    static constexpr float guitarVolts = 0.5f;   // 1,0 digital = 0,5 V (igual aos outros pedais)
    static constexpr float sat = 1.6f;           // o pré começa a saturar perto de 1,6 V
    std::vector<float> buf;
    int mask = 0, w = 0;
    double fs = 48000, lfo = 0.25, rateHz = 1.73, widthMs = 2.3;
    const double baseMs = 2.5;
    bool on = false, vib = false;
    float inGain = 1.0f, outComp = 1.0f, onGain = 0.0f, fadeStep = 0.001f;
    Biquad preLp, postLp, postLp2, lowMid, inHp;
    OnePole tilt;
};
} // namespace vlv::pedal

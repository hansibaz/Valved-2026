#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include "Biquad.h"
#include "Overdrive.h"
#include "FxCore.h"

namespace vlv
{
// Delay estéreo multi-modos, inspirado nos delays modernos de estúdio/pedal (v2):
//  - leitura com interpolação cúbica (repetições moduladas sem perder agudos)
//  - filtros de 12 dB/oitava no laço de realimentação
//  - Tape: wow (aleatório) + flutter, saturação com pré/de-ênfase (os agudos saturam antes,
//    como na fita), "head bump" nos graves e perda de agudos que aumenta com o AGE
//  - Analog: BBD com filtros anti-aliasing de 4 polos cujo corte cai com o tempo de atraso
//    (como o clock de um MN3005) e compander (o "respiro" típico dos analógicos)
class StereoDelay
{
public:
    enum Type { Digital = 0, Tape, Analog, Dual, PingPong, MultiTap, Reverse, Swell, Ducking, LoFi, NumTypes };

    static juce::StringArray typeNames()
    {
        return { "Digital", "Tape", "Analog", "Dual", "Ping-Pong", "Multi-Tap", "Reverse", "Swell", "Ducking", "Lo-Fi" };
    }

    // Nome do knob "especial" de cada tipo
    static const char* paramName (int t)
    {
        static const char* n[] = { "GRIT", "AGE", "LOSS", "TIME 2", "WIDTH", "PATTERN", "SMEAR", "RISE", "SENS", "BITS" };
        return n[juce::jlimit (0, NumTypes - 1, t)];
    }

    struct Settings
    {
        bool on = false;
        int type = Digital;
        float timeMs = 400.0f;
        float repeats = 4.0f;   // 0..10
        float mix = 30.0f;      // 0..100 %
        float tone = 6.0f;      // 0..10
        float mod = 2.0f;       // 0..10
        float param = 5.0f;     // 0..10
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        int sz = 1;
        while (sz < (int) (fs * 4.5)) sz <<= 1;
        size = sz; mask = sz - 1;
        bufL.assign ((size_t) size, 0.0f);
        bufR.assign ((size_t) size, 0.0f);
        reset();
    }

    void reset()
    {
        std::fill (bufL.begin(), bufL.end(), 0.0f);
        std::fill (bufR.begin(), bufR.end(), 0.0f);
        w = 0; dS = -1.0;
        for (auto& c : filt) for (auto& f : c) f.reset();
        bpL.reset(); bpR.reset(); compEnv[0] = compEnv[1] = 0.0f;
        env = envSlow = 0; swellGain = 1; duckGain = 1; revPhase = 0; lfo1 = lfo2 = 0;
        holdL = holdR = 0; holdCount = 0;
        inputGain = 0; wetGain = 0; dryGain = 1; idle = true; quietSamples = 0;
    }

    void process (float* L, float* R, int n, const Settings& s)
    {
        const bool on = s.on;
        if (! on && idle) return;
        if (on) { idle = false; quietSamples = 0; }

        const int type = juce::jlimit (0, NumTypes - 1, s.type);
        lastType = type;
        configure (s);

        const double target = juce::jlimit (1.0, (double) size * 0.45, (double) s.timeMs * 0.001 * fs);
        if (dS < 0) dS = target;
        const double glide = 1.0 - std::exp (-1.0 / ((type == Tape ? 0.25 : 0.06) * fs));

        const float mix = juce::jlimit (0.0f, 1.0f, s.mix / 100.0f);
        const float wetT = on ? (mix < 0.5f ? 2.0f * mix : 1.0f) : wetGain;   // com o pedal desligado, as repetições continuam (trails)
        const float dryT = on ? (mix < 0.5f ? 1.0f : 2.0f * (1.0f - mix)) : 1.0f;
        const float inT  = on ? 1.0f : 0.0f;
        const float sm = 1.0f / (0.02f * (float) fs);
        const float p = s.param / 10.0f;
        const float modDepth = s.mod / 10.0f;

        for (int i = 0; i < n; ++i)
        {
            dS += (target - dS) * glide;
            inputGain += juce::jlimit (-sm, sm, inT - inputGain);
            wetGain += juce::jlimit (-sm, sm, wetT - wetGain);
            dryGain += juce::jlimit (-sm, sm, dryT - dryGain);

            const float inL = L[i], inR = R != nullptr ? R[i] : L[i];
            const float mono = 0.5f * (inL + inR);

            // Envelope de entrada (para Swell e Ducking)
            const float a = std::abs (mono);
            env = a > env ? a + envAtk * (env - a) : a + envRel * (env - a);
            envSlow = a + 0.9995f * (envSlow - a);

            float din = mono * inputGain;
            if (type == Swell)
            {
                if (env > 0.02f && env > 2.5f * envSlow + 0.005f && swellGain > 0.6f) swellGain = 0.0f;
                swellGain += (1.0f - swellGain) * swellRate;
                din *= swellGain * swellGain;
            }

            // Modulação (chorus nas repetições; wow & flutter na fita)
            lfo1 += 0.55 / fs; if (lfo1 >= 1.0) lfo1 -= 1.0;
            lfo2 += 6.3 / fs;  if (lfo2 >= 1.0) lfo2 -= 1.0;
            double mod = modDepth * 0.0035 * fs * std::sin (2.0 * kPi * lfo1);
            if (type == Tape)   // wow (lento e irregular) + flutter (rápido e pequeno)
                mod = modDepth * 0.0025 * fs * wow.next() + (0.3 + p) * (0.0018 * fs * wow2.next() + 0.00022 * fs * std::sin (2.0 * kPi * lfo2));
            if (type == Analog)
                mod += 0.0006 * fs * bbdWander.next();

            const double dL = juce::jmax (2.0, dS + mod);
            const double dR = juce::jmax (2.0, (type == Dual ? dS * dualRatio : dS) + mod * 0.9);

            float yL, yR;
            if (type == Reverse)
            {
                yL = readReverse (bufL, dS);
                yR = readReverse (bufR, dS);
                revPhase += 1.0; if (revPhase >= juce::jmax (2000.0, dS)) revPhase = 0.0;
            }
            else if (type == MultiTap)
            {
                yL = yR = 0.0f;
                for (int k = 0; k < 4; ++k)
                {
                    const float tg = tapGain[k];
                    if (tg <= 0.0f) continue;
                    const double d = juce::jmax (2.0, dS * tapPos[k] + mod);
                    const float v = read (bufL, d) * tg;
                    if (k % 2 == 0) { yL += v; yR += v * 0.6f; } else { yR += v; yL += v * 0.6f; }
                }
            }
            else
            {
                yL = read (bufL, dL);
                yR = read (bufR, dR);
            }

            // Laço de realimentação com o "caráter" de cada tipo
            float fL = (type == MultiTap) ? read (bufL, dL) : yL;
            float fR = (type == MultiTap) ? fL : yR;
            fL = colour (fL, 0);
            fR = colour (fR, 1);

            float wL, wR;
            if (type == PingPong)
            {
                wL = din + fR * fb;
                wR = fL * fb;
            }
            else if (type == Dual)
            {
                wL = din + fL * fb;
                wR = din + fR * fb * 0.92f;
            }
            else
            {
                wL = din + fL * fb;
                wR = (type == Reverse) ? din + fR * fb : wL;
            }
            bufL[(size_t) w] = wL;
            bufR[(size_t) w] = wR;
            w = (w + 1) & mask;

            // Saída molhada
            float oL = yL, oR = yR;
            if (type == PingPong)
            {
                const float width = 0.3f + 0.7f * p;
                const float mid = 0.5f * (oL + oR), side = 0.5f * (oL - oR) * width;
                oL = mid + side; oR = mid - side;
            }
            if (type == LoFi)
            {
                if (--holdCount <= 0) { holdCount = loHold; holdL = crush (oL); holdR = crush (oR); }
                oL = bpL.process (holdL); oR = bpR.process (holdR);
            }
            if (type == Ducking)
            {
                const float duckT = 1.0f - juce::jlimit (0.0f, 0.95f, env * duckSens);
                duckGain = duckT < duckGain ? duckT : duckT + 0.9997f * (duckGain - duckT);
                oL *= duckGain; oR *= duckGain;
            }

            L[i] = inL * dryGain + oL * wetGain;
            if (R != nullptr) R[i] = inR * dryGain + oR * wetGain;

            if (! on)
            {
                if (std::abs (oL) + std::abs (oR) < 1.0e-5f) ++quietSamples; else quietSamples = 0;
            }
        }

        if (! on && quietSamples > (int) fs) { idle = true; reset(); }
    }

private:
    void configure (const Settings& s)
    {
        const int type = juce::jlimit (0, NumTypes - 1, s.type);
        const float p = juce::jlimit (0.0f, 1.0f, s.param / 10.0f);
        const float t = juce::jlimit (0.0f, 1.0f, s.tone / 10.0f);
        const float rep = juce::jlimit (0.0f, 1.0f, s.repeats / 10.0f);

        const bool saturating = (type == Tape || type == Analog);
        fb = saturating ? 1.06f * rep : 0.97f * rep;
        if (type == Tape) fb /= std::sqrt (dbToGain (2.5f * (0.5f + p)));   // head bump: no máximo dispara nos graves, como uma fita de verdade

        double toneHz = 1200.0 * std::pow (12.0, (double) t);     // 1,2 kHz .. 14 kHz
        if (type == Tape)   toneHz = juce::jmin (toneHz, 10000.0 - 7000.0 * p);
        if (type == Analog)
        {
            // BBD de 4096 estágios: o corte acompanha o clock (cai quando o tempo aumenta)
            const double clockNyq = 4096.0 / (2.0 * juce::jmax (0.02, s.timeMs * 0.001)) * 0.5;
            toneHz = juce::jmin (toneHz, juce::jlimit (1500.0, 8000.0, 0.8 * clockNyq) * (1.0 - 0.5 * p));
        }
        for (int c = 0; c < 2; ++c)
        {
            Biquad* f = filt[c];
            f[0].highpass (fs, type == Tape ? 90.0 : 60.0, 0.707);
            f[1].highpass (fs, type == Tape ? 90.0 : 60.0, 0.707);
            f[2].lowpass (fs, toneHz, 0.707);
            f[3].lowpass (fs, toneHz, 0.707);
            f[4].lowpass (fs, toneHz * 1.1, 0.6);
            f[5].highShelf (fs, 3000.0, 6.0);                   // pré-ênfase da fita
            f[6].highShelf (fs, 3000.0, -6.0);                  // de-ênfase
            f[7].peak (fs, 95.0, 1.0, 2.5 * (0.5 + p));         // head bump
        }
        wow.setRate (fs, 0.45 + 0.6 * p); wow2.setRate (fs, 2.2); bbdWander.setRate (fs, 0.35);

        grit = type == Digital ? p : (type == Tape ? 0.4f + 0.6f * p : (type == Analog ? 0.5f + 0.5f * p : 0.0f));
        dualRatio = 0.25 + 0.75 * (double) p;
        swellRate = 1.0f / ((0.05f + 1.8f * p * p) * (float) fs) * 4.0f;
        duckSens = 2.0f + 30.0f * p;
        smear = p;

        // Lo-Fi: menos bits e taxa de amostragem reduzida
        loBits = 12.0f - 8.0f * p;
        loHold = 1 + (int) (p * 7.0f * (float) (fs / 48000.0));
        bpL.peak (fs, 1200.0, 0.5, 4.0); bpR.peak (fs, 1200.0, 0.5, 4.0);

        // Padrões do Multi-Tap
        const int pattern = juce::jlimit (0, 3, (int) (p * 3.999f));
        static const float pos[4][4] = { { 0.25f, 0.5f, 0.75f, 1.0f }, { 0.333f, 0.666f, 1.0f, 0.0f },
                                         { 0.375f, 0.5f, 0.875f, 1.0f }, { 0.125f, 0.25f, 0.625f, 1.0f } };
        static const float gn[4][4] = { { 0.5f, 0.65f, 0.8f, 1.0f }, { 0.7f, 0.85f, 1.0f, 0.0f },
                                        { 0.8f, 0.5f, 0.7f, 1.0f }, { 0.4f, 0.6f, 0.8f, 1.0f } };
        for (int k = 0; k < 4; ++k) { tapPos[k] = pos[pattern][k]; tapGain[k] = gn[pattern][k]; }
    }

    inline float colour (float x, int ch) noexcept
    {
        Biquad* f = filt[ch];
        x = f[1].process (f[0].process (x));                    // passa-altas 12 dB/oit
        if (lastType == Tape)
        {
            // pré-ênfase -> saturação da fita -> de-ênfase (agudos comprimem antes)
            x = f[5].process (x);
            const float drive = 1.0f + 2.5f * grit;
            x = std::tanh (x * drive) / drive * (1.0f + 0.25f * grit);
            x = f[6].process (x);
            x = f[7].process (x);                               // head bump (graves)
            x = f[2].process (x);                               // perda de agudos (AGE/TONE)
            return x;
        }
        if (lastType == Analog)
        {
            // compander: comprime antes do BBD, expande depois (o "respiro")
            const float a = std::abs (x);
            compEnv[ch] = a > compEnv[ch] ? a + 0.995f * (compEnv[ch] - a) : a + 0.9995f * (compEnv[ch] - a);
            const float cg = 1.0f / std::sqrt (std::max (compEnv[ch], 0.02f) / 0.25f);
            x *= cg;
            x = f[3].process (f[2].process (x));                // anti-aliasing 4 polos (clock do BBD)
            x = std::tanh (x * 1.2f) / 1.2f;
            x = f[4].process (x);                               // reconstrução
            x /= cg;
            return std::tanh (x);
        }
        x = f[2].process (x);                                   // TONE 12 dB/oit
        if (grit > 0.0f)
        {
            const float drive = 1.0f + 3.0f * grit;
            x = x + (std::tanh (x * drive) / drive - x) * std::min (1.0f, grit * 1.5f);
        }
        return x;
    }

    inline float crush (float x) const noexcept
    {
        const float q = std::pow (2.0f, loBits - 1.0f);
        return std::round (x * q) / q;
    }

    inline float read (const std::vector<float>& b, double delay) const noexcept
    {
        // interpolação cúbica (Hermite): modulação limpa, sem perda de agudos
        const double pos = (double) w - delay;
        const double fl = std::floor (pos);
        const float t = (float) (pos - fl);
        const int i = (int) fl;
        const float ym1 = b[(size_t) ((i - 1) & mask)], y0 = b[(size_t) (i & mask)];
        const float y1 = b[(size_t) ((i + 1) & mask)], y2 = b[(size_t) ((i + 2) & mask)];
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * t + c2) * t + c1) * t + y0;
    }

    // Duas cabeças de leitura andando para trás, com crossfade
    inline float readReverse (const std::vector<float>& b, double len) const noexcept
    {
        len = juce::jmax (2000.0, len);
        float out = 0.0f;
        for (int h = 0; h < 2; ++h)
        {
            double ph = revPhase + (h == 0 ? 0.0 : len * 0.5);
            if (ph >= len) ph -= len;
            const double x = ph / len;
            const double win = smear > 0.5f ? std::sin (kPi * x) * std::sin (kPi * x)
                                            : juce::jmin (1.0, juce::jmin (x, 1.0 - x) * (8.0 - 12.0 * smear));
            out += (float) win * read (b, 2.0 + 2.0 * ph);
        }
        return smear > 0.5f ? out : out * 0.75f;
    }

    double fs = 48000;
    int size = 1, mask = 0, w = 0, lastType = -1;
    std::vector<float> bufL, bufR;
    double dS = -1.0, lfo1 = 0, lfo2 = 0, revPhase = 0, dualRatio = 0.75;
    float fb = 0.5f, grit = 0, swellRate = 0.001f, duckSens = 10.0f, smear = 0.5f;
    float env = 0, envSlow = 0, swellGain = 1, duckGain = 1;
    const float envAtk = 0.9f, envRel = 0.9995f;
    float loBits = 8, holdL = 0, holdR = 0; int loHold = 1, holdCount = 0;
    float tapPos[4] {}, tapGain[4] {};
    float inputGain = 0, wetGain = 0, dryGain = 1;
    bool idle = true; int quietSamples = 0;
    Biquad filt[2][8];          // por canal: HP, HP, LP, LP, LP, pré-ênfase, de-ênfase, head bump
    Biquad bpL, bpR;
    float compEnv[2] {};
    fx::SmoothRandom wow { 31u }, wow2 { 47u }, bbdWander { 53u };
};
} // namespace vlv

#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>
#include "Biquad.h"
#include "Overdrive.h"
#include "PitchShifter.h"

namespace vlv
{
// Reverb algorítmico (rede de realimentação com 8 linhas + difusores),
// com vários "algoritmos" no estilo dos reverbs modernos de pedal/estúdio.
class AmbientReverb
{
public:
    enum Type { Room = 0, Hall, Plate, Spring, Shimmer, Bloom, Cloud, Swell, Gated, NumTypes };

    static juce::StringArray typeNames()
    {
        return { "Room", "Hall", "Plate", "Spring", "Shimmer", "Bloom", "Cloud", "Swell", "Gated" };
    }

    static const char* paramName (int t)
    {
        static const char* n[] = { "SIZE", "SIZE", "LOW END", "DWELL", "AMOUNT", "BLOOM", "DIFFUSION", "RISE", "GATE" };
        return n[juce::jlimit (0, NumTypes - 1, t)];
    }

    struct Settings
    {
        bool on = false;
        int type = Hall;
        float decay = 5.0f;       // 0..10
        float preDelayMs = 20.0f; // 0..250
        float mix = 25.0f;        // 0..100 %
        float tone = 6.0f;        // 0..10
        float mod = 3.0f;         // 0..10
        float param = 5.0f;       // 0..10
    };

    // Tempo de decaimento (RT60) mínimo/máximo em segundos para cada tipo
    static std::pair<float, float> decayRange (int t)
    {
        static const float r[NumTypes][2] = { { 0.3f, 3.0f }, { 0.8f, 12.0f }, { 0.5f, 8.0f }, { 0.8f, 5.0f }, { 2.0f, 20.0f },
                                              { 2.0f, 20.0f }, { 3.0f, 30.0f }, { 1.0f, 12.0f }, { 1.0f, 4.0f } };
        const int i = juce::jlimit (0, NumTypes - 1, t);
        return { r[i][0], r[i][1] };
    }

    static float decaySeconds (int t, float knob)
    {
        const auto [lo, hi] = decayRange (t);
        return lo * std::pow (hi / lo, juce::jlimit (0.0f, 1.0f, knob / 10.0f));
    }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const double sr = fs / 48000.0;
        for (auto& l : lines) { l.buf.assign ((size_t) nextPow2 ((int) (0.40 * fs)), 0.0f); l.mask = (int) l.buf.size() - 1; }
        pre.assign ((size_t) nextPow2 ((int) (0.30 * fs)), 0.0f); preMask = (int) pre.size() - 1;

        static const int diffLen[4] = { 142, 107, 379, 277 };
        for (int k = 0; k < 4; ++k) diff[(size_t) k].init ((int) (diffLen[k] * sr * 1.6));
        static const int bloomLen[4] = { 1559, 2297, 3511, 4931 };
        for (int k = 0; k < 4; ++k) bloomAp[(size_t) k].init ((int) (bloomLen[k] * sr));
        for (int k = 0; k < (int) chirp.size(); ++k) chirp[(size_t) k].init (2 + k % 3);

        shimmer.prepare (fs);
        shimmer.setSemitones (12);
        reset();
    }

    void reset()
    {
        for (auto& l : lines) { std::fill (l.buf.begin(), l.buf.end(), 0.0f); l.damp.reset(); }
        std::fill (pre.begin(), pre.end(), 0.0f);
        for (auto& d : diff) d.clear();
        for (auto& d : bloomAp) d.clear();
        for (auto& d : chirp) d.clear();
        w = 0; preW = 0;
        outLpL.reset(); outLpR.reset(); outHpL.reset(); outHpR.reset(); shimHp.reset();
        springBp.reset(); springBp2.reset();
        env = 0; envSlow = 0; swellGain = 1; gateGain = 0; gateHold = 0;
        shimL = shimR = 0;
        inputGain = 0; wetGain = 0; dryGain = 1; idle = true; quietSamples = 0;
        for (auto& p : lfo) p = 0;
    }

    void process (float* L, float* R, int n, const Settings& s)
    {
        if (! s.on && idle) return;
        if (s.on) { idle = false; quietSamples = 0; }
        configure (s);

        const float mix = juce::jlimit (0.0f, 1.0f, s.mix / 100.0f);
        const float wetT = s.on ? (mix < 0.5f ? 2.0f * mix : 1.0f) : wetGain;
        const float dryT = s.on ? (mix < 0.5f ? 1.0f : 2.0f * (1.0f - mix)) : 1.0f;
        const float inT = s.on ? 1.0f : 0.0f;
        const float sm = 1.0f / (0.02f * (float) fs);
        const int preD = juce::jlimit (1, preMask - 1, (int) (s.preDelayMs * 0.001f * (float) fs));

        for (int i = 0; i < n; ++i)
        {
            inputGain += juce::jlimit (-sm, sm, inT - inputGain);
            wetGain += juce::jlimit (-sm, sm, wetT - wetGain);
            dryGain += juce::jlimit (-sm, sm, dryT - dryGain);

            const float inL = L[i], inR = R != nullptr ? R[i] : L[i];
            const float mono = 0.5f * (inL + inR);
            const float a = std::abs (mono);
            env = a > env ? a + 0.9f * (env - a) : a + 0.9995f * (env - a);
            envSlow = a + 0.9998f * (envSlow - a);

            // Pré-delay
            pre[(size_t) preW] = mono * inputGain;
            float x = pre[(size_t) ((preW - preD) & preMask)];
            preW = (preW + 1) & preMask;

            switch (type)
            {
                case Swell:
                    if (env > 0.02f && env > 2.5f * envSlow + 0.005f && swellGain > 0.6f) swellGain = 0.0f;
                    swellGain += (1.0f - swellGain) * swellRate;
                    x *= swellGain * swellGain;
                    break;
                case Spring:
                {
                    // Molas: entrada saturada ("dwell") + dispersão (chirp) + banda limitada
                    x = std::tanh (x * dwell) / juce::jmax (1.0f, dwell * 0.6f);
                    for (auto& c : chirp) x = c.process (x, 0.62f);
                    x = springBp2.process (springBp.process (x));
                    break;
                }
                case Bloom:
                    for (auto& d : bloomAp) x = d.process (x, bloomG);
                    break;
                case Shimmer:
                    x += shimHp.process (0.5f * (shimL + shimR)) * shimAmt;
                    break;
                default: break;
            }

            // Difusão de entrada
            for (auto& d : diff) x = d.process (x, diffG);

            // Rede de realimentação (8 linhas, matriz de Householder)
            float o[8];
            float sum = 0.0f;
            for (int k = 0; k < 8; ++k)
            {
                auto& l = lines[(size_t) k];
                double d = l.len;
                if (k == 1 || k == 4 || k == 6)
                {
                    lfo[(size_t) k] += lfoRate[k] / fs;
                    if (lfo[(size_t) k] >= 1.0) lfo[(size_t) k] -= 1.0;
                    d += modDepth * std::sin (2.0 * kPi * lfo[(size_t) k]);
                }
                o[k] = l.damp.lp (l.read (w, d)) * l.g;
                sum += o[k];
            }
            const float hh = sum * 0.25f;
            for (int k = 0; k < 8; ++k)
            {
                auto& l = lines[(size_t) k];
                const float sign = (k & 1) ? -1.0f : 1.0f;
                l.buf[(size_t) (w & l.mask)] = x * sign * 0.5f + (o[k] - hh);
            }
            w = (w + 1) & 0xffffff;

            float yL = (o[0] - o[2] + o[4] - o[6]) * 0.6f;
            float yR = (o[1] - o[3] + o[5] - o[7]) * 0.6f;

            if (type == Shimmer)
            {
                const float s = shimmer.process (0.5f * (yL + yR));
                shimL = s; shimR = s;
            }
            if (type == Gated)
            {
                if (env > 0.01f) gateHold = gateTime;
                const float target = gateHold > 0 ? 1.0f : 0.0f;
                if (gateHold > 0) --gateHold;
                gateGain = target + (target > gateGain ? 0.5f : 0.9992f) * (gateGain - target);
                yL *= gateGain; yR *= gateGain;
            }

            yL = outHpL.hp (outLpL.lp (yL));
            yR = outHpR.hp (outLpR.lp (yR));

            L[i] = inL * dryGain + yL * wetGain * outScale;
            if (R != nullptr) R[i] = inR * dryGain + yR * wetGain * outScale;

            if (! s.on)
            {
                if (std::abs (yL) + std::abs (yR) < 1.0e-5f) ++quietSamples; else quietSamples = 0;
            }
        }
        if (! s.on && quietSamples > (int) (fs * 2)) { idle = true; reset(); }
    }

private:
    static int nextPow2 (int v) { int p = 1; while (p < v) p <<= 1; return p; }

    struct Line
    {
        std::vector<float> buf; int mask = 0;
        double len = 1000; float g = 0.5f;
        OnePole damp;
        inline float read (int wpos, double d) const noexcept
        {
            const double pos = (double) wpos - d;
            const double fl = std::floor (pos);
            const float t = (float) (pos - fl);
            const int i = (int) fl;
            const float a = buf[(size_t) (i & mask)], b = buf[(size_t) ((i + 1) & mask)];
            return a + (b - a) * t;
        }
    };

    struct Allpass
    {
        std::vector<float> b; int idx = 0;
        void init (int len) { b.assign ((size_t) juce::jmax (1, len), 0.0f); idx = 0; }
        void clear() { std::fill (b.begin(), b.end(), 0.0f); idx = 0; }
        inline float process (float x, float g) noexcept
        {
            const float d = b[(size_t) idx];
            const float v = x - g * d;
            b[(size_t) idx] = v;
            if (++idx >= (int) b.size()) idx = 0;
            return d + g * v;
        }
    };

    void configure (const Settings& s)
    {
        type = juce::jlimit (0, NumTypes - 1, s.type);
        const float p = juce::jlimit (0.0f, 1.0f, s.param / 10.0f);
        const float t = juce::jlimit (0.0f, 1.0f, s.tone / 10.0f);
        const float m = juce::jlimit (0.0f, 1.0f, s.mod / 10.0f);

        static const double baseMs[8] = { 29.7, 37.1, 41.1, 43.7, 47.9, 53.3, 59.9, 67.1 };
        float size = 1.0f;
        switch (type)
        {
            case Room:    size = 0.25f + 0.45f * p; break;
            case Hall:    size = 0.8f + 0.9f * p; break;
            case Plate:   size = 0.55f; break;
            case Spring:  size = 0.45f; break;
            case Shimmer: size = 1.5f; break;
            case Bloom:   size = 1.4f; break;
            case Cloud:   size = 2.3f; break;
            case Swell:   size = 1.3f; break;
            case Gated:   size = 0.5f; break;
            default: break;
        }
        const float rt = decaySeconds (type, s.decay);
        const double damping = 2500.0 * std::pow (6.0, (double) t);   // 2,5 kHz .. 15 kHz
        for (int k = 0; k < 8; ++k)
        {
            auto& l = lines[(size_t) k];
            l.len = juce::jmin ((double) l.mask - 64.0, baseMs[k] * 0.001 * fs * size);
            l.g = (float) std::pow (10.0, -3.0 * l.len / (rt * fs));
            l.damp.setCutoff (fs, type == Plate ? damping * 1.4 : (type == Spring ? juce::jmin (damping, 5000.0) : damping));
        }

        diffG = type == Plate ? 0.75f : (type == Cloud ? 0.55f + 0.25f * p : (type == Room ? 0.6f : 0.65f));
        modDepth = (0.5 + 14.0 * m) * (fs / 48000.0) * (type == Cloud || type == Shimmer ? 1.5 : 1.0);
        bloomG = 0.55f + 0.3f * p;
        dwell = 1.0f + 6.0f * p;
        shimAmt = 0.12f + 0.4f * p;
        swellRate = 4.0f / ((0.1f + 2.5f * p * p) * (float) fs);
        gateTime = (int) ((0.12f + 0.6f * p) * (float) fs);

        const double lowCut = type == Plate ? 40.0 + 360.0 * (1.0 - p) : (type == Spring ? 180.0 : 70.0);
        outHpL.setCutoff (fs, lowCut); outHpR.setCutoff (fs, lowCut);
        const double hi = 3000.0 * std::pow (6.0, (double) t);
        outLpL.setCutoff (fs, hi); outLpR.setCutoff (fs, hi);
        shimHp.highpass (fs, 600.0, 0.7);
        springBp.highpass (fs, 200.0, 0.7);
        springBp2.lowpass (fs, 4500.0, 0.9);
        outScale = type == Spring ? 1.6f : (type == Room ? 1.3f : (type == Cloud || type == Bloom || type == Shimmer ? 0.75f : 1.0f));
    }

    double fs = 48000;
    int type = Hall;
    std::array<Line, 8> lines;
    std::vector<float> pre; int preMask = 0, preW = 0;
    std::array<Allpass, 4> diff, bloomAp;
    std::array<Allpass, 12> chirp;
    PitchShifter shimmer;
    int w = 0;
    std::array<double, 8> lfo {};
    const double lfoRate[8] = { 0, 0.31, 0, 0, 0.47, 0, 0.73, 0 };
    double modDepth = 2.0;
    float diffG = 0.65f, bloomG = 0.7f, dwell = 2.0f, shimAmt = 0.4f, swellRate = 0.001f, outScale = 1.0f;
    int gateTime = 10000, gateHold = 0;
    float env = 0, envSlow = 0, swellGain = 1, gateGain = 0, shimL = 0, shimR = 0;
    float inputGain = 0, wetGain = 0, dryGain = 1;
    bool idle = true; int quietSamples = 0;
    OnePole outLpL, outLpR, outHpL, outHpR;
    Biquad shimHp, springBp, springBp2;
};
} // namespace vlv

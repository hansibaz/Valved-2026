#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>
#include "Biquad.h"
#include "Overdrive.h"
#include "FxCore.h"

namespace vlv
{
// =====================================================================================
// Reverb de estúdio (v2)
//  - Room / Hall / Swell / Gated / Bloom / Cloud / Shimmer: rede de realimentação (FDN) de
//    16 linhas com matriz de Hadamard, estéreo real, decaimento separado para graves e agudos
//    (filtros de absorção em cada linha), modulação aleatória suave em todas as linhas com
//    leitura cúbica, difusores de entrada e reflexões iniciais (Room/Hall).
//  - Plate: o tanque clássico de Jon Dattorro ("Effect Design", 1997), em figura de oito, com
//    allpasses modulados e as saídas estéreo originais.
//  - Spring: modelo de mola com dispersão (cascata de allpasses "esticados", que dá o "boing"),
//    duas molas (L/R) e o driver valvulado na entrada (DWELL).
//  - Shimmer (oitava) e Shimmer 5th (quinta + oitava): o pitch shifter espectral (phase vocoder) fica DENTRO
//    do laço do reverb, como nos pedais Strymon BigSky: o som transposto volta para o tanque e
//    é transposto de novo, formando a "cascata" de oitavas que sobe e se espalha.
// =====================================================================================
class AmbientReverb
{
public:
    enum Type { Room = 0, Hall, Plate, Spring, Shimmer, Shimmer5, Bloom, Cloud, Swell, Gated, NumTypes };

    static juce::StringArray typeNames()
    {
        return { "Room", "Hall", "Plate", "Spring", "Shimmer", "Shimmer 5th", "Bloom", "Cloud", "Swell", "Gated" };
    }

    static const char* paramName (int t)
    {
        static const char* n[] = { "SIZE", "SIZE", "LOW END", "DWELL", "AMOUNT", "AMOUNT", "BLOOM", "DIFFUSION", "RISE", "GATE" };
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
        static const float r[NumTypes][2] = { { 0.3f, 3.0f }, { 0.8f, 14.0f }, { 0.5f, 9.0f }, { 0.8f, 5.0f }, { 2.0f, 25.0f },
                                              { 2.0f, 25.0f }, { 2.0f, 20.0f }, { 3.0f, 30.0f }, { 1.0f, 12.0f }, { 1.0f, 4.0f } };
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
        const double k = fs / 48000.0;
        for (int i = 0; i < 16; ++i)
        {
            lines[(size_t) i].init ((int) (0.25 * fs));
            lfo[(size_t) i] = fx::SmoothRandom (101u + 17u * (uint32_t) i);
        }
        for (int i = 0; i < 8; ++i) { cloudLfo[(size_t) i] = fx::SmoothRandom (701u + 13u * (uint32_t) i); cloudLfo[(size_t) i].setRate (fs, 0.21 + 0.07 * i); }
        preL.init ((int) (0.40 * fs)); preR.init ((int) (0.40 * fs));
        // difusores de entrada (L e R com tamanhos diferentes = estéreo decorrelacionado)
        static const double dl[2][4] = { { 4.771, 3.595, 12.73, 9.307 }, { 5.123, 3.881, 12.03, 9.851 } };
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 4; ++i)
                diff[c][(size_t) i].init ((int) (0.02 * fs), dl[c][i] * 0.001 * fs, 0.7f);
        // cadeias longas do Bloom e do Cloud
        for (int c = 0; c < 2; ++c)
        {
            for (int i = 0; i < 6; ++i) bloomAp[c][(size_t) i].init ((int) (0.20 * fs), 0.05 * fs, 0.7f);
            for (int i = 0; i < 8; ++i) cloudAp[c][(size_t) i].init ((int) (0.12 * fs), 0.01 * fs, 0.65f);
        }
        // Plate (Dattorro): tamanhos originais a 29761 Hz, escalados
        const double dk = fs / 29761.0;
        static const int pin[4] = { 142, 107, 379, 277 };
        for (int i = 0; i < 4; ++i) plIn[(size_t) i].init ((int) (pin[i] * dk) + 4, pin[i] * dk, i < 2 ? 0.75f : 0.625f);
        plAp1[0].init ((int) ((672 + 64) * dk), 672 * dk, -0.7f);  plAp1[1].init ((int) ((908 + 64) * dk), 908 * dk, -0.7f);
        plD1[0].init ((int) (4453 * dk) + 8);                     plD1[1].init ((int) (4217 * dk) + 8);
        plAp2[0].init ((int) (1800 * dk) + 8, 1800 * dk, 0.5f);   plAp2[1].init ((int) (2656 * dk) + 8, 2656 * dk, 0.5f);
        plD2[0].init ((int) (3720 * dk) + 8);                     plD2[1].init ((int) (3163 * dk) + 8);
        plLen[0] = 4453 * dk; plLen[1] = 4217 * dk; plLen2[0] = 3720 * dk; plLen2[1] = 3163 * dk;
        plLoop = (672 + 4453 + 1800 + 3720 + 908 + 4217 + 2656 + 3163) * dk / fs;
        // Mola: duas molas, cada uma com a linha de trânsito e a cascata dispersiva
        for (int c = 0; c < 2; ++c)
        {
            springDl[c].init ((int) (0.08 * fs));
            for (auto& s : springAp[c]) s.init (springK);
        }
        // shimmer: um shifter de oitava e um de quinta por canal
        for (int c = 0; c < 2; ++c) { shimOct[c].prepare (fs, 2.0); shim5[c].prepare (fs, std::pow (2.0, 7.0 / 12.0)); }
        juce::ignoreUnused (k);
        lastType = -1;
        reset();
    }

    void reset()
    {
        for (auto& l : lines) l.clear();
        for (auto& f : damp) f.reset();
        preL.clear(); preR.clear();
        for (int c = 0; c < 2; ++c)
        {
            for (auto& a : diff[c]) a.clear();
            for (auto& a : bloomAp[c]) a.clear();
            for (auto& a : cloudAp[c]) a.clear();
            springDl[c].clear();
            for (auto& s : springAp[c]) s.clear();
            shimOct[c].clear(); shim5[c].clear();
            plAp1[c].clear(); plD1[c].clear(); plAp2[c].clear(); plD2[c].clear();
            plDampZ[c] = 0.0f; plFeed[c] = 0.0f;
        }
        for (auto& a : plIn) a.clear();
        plBwZ = 0.0f;
        for (auto* b : { &hpL, &hpR, &lpL, &lpR, &hp2L, &hp2R, &lp2L, &lp2R, &shimHp, &shimLp, &shimHp2, &shimLp2,
                         &springHp, &springLp, &springHp2, &springLp2 })
            b->reset();
        env = 0; envSlow = 0; swellGain = 1; gateGain = 0; gateHold = 0;
        shimFb[0] = shimFb[1] = 0.0f; springFb[0] = springFb[1] = 0.0f;
        inputGain = 0; wetGain = 0; dryGain = 1; idle = true; quietSamples = 0;
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
        const double preD = juce::jlimit (1.0, 0.39 * fs, (double) s.preDelayMs * 0.001 * fs);
        curPreD = juce::jmin (preD, 0.39 * fs - 0.09 * fs);

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

            // Pré-delay (estéreo)
            preL.push (inL * inputGain); preR.push (inR * inputGain);
            float xL = preL.read (preD), xR = preR.read (preD);

            if (type == Swell)
            {
                if (env > 0.02f && env > 2.5f * envSlow + 0.005f && swellGain > 0.6f) swellGain = 0.0f;
                swellGain += (1.0f - swellGain) * swellRate;
                const float g = swellGain * swellGain;
                xL *= g; xR *= g;
            }

            float yL = 0.0f, yR = 0.0f;
            if (type == Plate)       processPlate (0.5f * (xL + xR), yL, yR);
            else if (type == Spring) processSpring (xL, xR, yL, yR);
            else                     processFdn (xL, xR, yL, yR);

            if (type == Gated)
            {
                if (env > 0.01f) gateHold = gateTime;
                const float target = gateHold > 0 ? 1.0f : 0.0f;
                if (gateHold > 0) --gateHold;
                gateGain = target + (target > gateGain ? 0.5f : 0.9992f) * (gateGain - target);
                yL *= gateGain; yR *= gateGain;
            }

            // EQ de saída (12 dB/oitava nos dois extremos)
            yL = lp2L.process (lpL.process (hp2L.process (hpL.process (yL))));
            yR = lp2R.process (lpR.process (hp2R.process (hpR.process (yR))));

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
    // ---------------- FDN 16 linhas ----------------
    inline void processFdn (float xL, float xR, float& yL, float& yR) noexcept
    {
        // Reflexões iniciais (Room/Hall/Swell/Gated): ecos discretos das paredes antes da cauda
        float erL = 0.0f, erR = 0.0f;
        if (erLevel > 0.0f)
        {
            for (int k = 0; k < 12; ++k)
            {
                erL += erGain[k] * preL.read (curPreD + erTapL[k]);
                erR += erGain[k] * preR.read (curPreD + erTapR[k]);
            }
            erL *= erLevel; erR *= erLevel;
        }

        // Shimmer: o som transposto da cauda volta para a entrada do tanque
        if (type == Shimmer || type == Shimmer5)
        {
            xL += shimFb[0]; xR += shimFb[1];
        }

        // Difusão de entrada
        float dL = xL + 0.5f * erL, dR = xR + 0.5f * erR;
        for (int k = 0; k < 4; ++k) { dL = diff[0][(size_t) k].process (dL); dR = diff[1][(size_t) k].process (dR); }

        if (type == Bloom)
            for (int k = 0; k < 6; ++k)
            {
                dL = bloomAp[0][(size_t) k].process (dL);
                dR = bloomAp[1][(size_t) k].process (dR);
            }
        if (type == Cloud)
            for (int k = 0; k < 8; ++k)
            {
                const double m = cloudMod * cloudLfo[(size_t) k].next();
                dL = cloudAp[0][(size_t) k].process (dL, m);
                dR = cloudAp[1][(size_t) k].process (dR, -m);
            }

        // Leitura das 16 linhas (modulação aleatória suave, leitura cúbica) + absorção
        float o[16];
        for (int k = 0; k < 16; ++k)
        {
            const double d = lineLen[k] + modDepth * lfo[(size_t) k].next();
            o[k] = damp[(size_t) k].process (lines[(size_t) k].read (d));
        }
        // Saídas estéreo: linhas diferentes da matriz de Hadamard (sinais ortogonais)
        float sL = 0.0f, sR = 0.0f;
        for (int k = 0; k < 16; ++k) { sL += outSignL[k] * o[k]; sR += outSignR[k] * o[k]; }
        yL = sL * 0.25f + erL; yR = sR * 0.25f + erR;

        // Mistura e realimentação
        fx::hadamard16 (o);
        for (int k = 0; k < 16; ++k)
        {
            const float in = (k & 1) ? dR : dL;
            lines[(size_t) k].push (fx::softClip (o[k] + inSign[k] * in * 0.5f));
        }

        if (type == Shimmer || type == Shimmer5)
        {
            // o shimmer pega a cauda, tira graves e agudos ásperos e transpõe
            const float sLin = shimLp.process (shimHp.process (yL));
            const float sRin = shimLp2.process (shimHp2.process (yR));
            float pL = shimOct[0].process (sLin), pR = shimOct[1].process (sRin);
            if (type == Shimmer5)
            {
                pL = 0.6f * pL + 0.6f * shim5[0].process (sLin);
                pR = 0.6f * pR + 0.6f * shim5[1].process (sRin);
            }
            shimFb[0] = fx::softClip (pL * shimAmt);
            shimFb[1] = fx::softClip (pR * shimAmt);
            yL += pL * shimOut; yR += pR * shimOut;
        }
    }

    // ---------------- Plate (Dattorro) ----------------
    inline void processPlate (float x, float& yL, float& yR) noexcept
    {
        plBwZ = x + plBw * (plBwZ - x);                     // banda de entrada
        float v = plBwZ;
        for (auto& a : plIn) v = a.process (v);             // 4 difusores de entrada
        const float mod0 = (float) (plModDepth * plLfo0.next()), mod1 = (float) (plModDepth * plLfo1.next());
        float out[2];
        for (int c = 0; c < 2; ++c)
        {
            // entrada de cada metade = difusão + saída da outra metade
            float t = v + plFeed[1 - c];
            t = plAp1[c].process (t, c == 0 ? mod0 : mod1);
            plD1[c].push (t);
            float u = plD1[c].tap ((int) plLen[c]);
            plDampZ[c] = u + plDamp * (plDampZ[c] - u);     // amortecimento (agudos)
            u = plDampZ[c] * plDecay;
            u = plAp2[c].process (u);
            plD2[c].push (u);
            out[c] = plD2[c].tap ((int) plLen2[c]) * plDecay;
        }
        plFeed[0] = out[0]; plFeed[1] = out[1];
        const double k = fs / 29761.0;
        auto t1 = [&] (int c, int s) { return plD1[c].tap ((int) (s * k) + 1); };
        auto t2 = [&] (int c, int s) { return plAp2[c].d.tap ((int) (s * k) + 1); };
        auto t3 = [&] (int c, int s) { return plD2[c].tap ((int) (s * k) + 1); };
        yL = 0.6f * ( t1 (1, 266) + t1 (1, 2974) - t2 (1, 1913) + t3 (1, 1996) - t1 (0, 1990) - t2 (0, 187) - t3 (0, 1066));
        yR = 0.6f * ( t1 (0, 353) + t1 (0, 3627) - t2 (0, 1228) + t3 (0, 2673) - t1 (1, 2111) - t2 (1, 335) - t3 (1, 121));
    }

    // ---------------- Spring ----------------
    struct StretchedAp      // H(z) = (a + z^-K) / (1 + a z^-K)
    {
        std::vector<float> x, y; int i = 0, K = 4;
        void init (int k) { K = k; x.assign ((size_t) k, 0.0f); y.assign ((size_t) k, 0.0f); i = 0; }
        void clear() { std::fill (x.begin(), x.end(), 0.0f); std::fill (y.begin(), y.end(), 0.0f); i = 0; }
        inline float process (float in, float a) noexcept
        {
            const float xd = x[(size_t) i], yd = y[(size_t) i];
            const float out = a * in + xd - a * yd;
            x[(size_t) i] = in; y[(size_t) i] = out;
            if (++i >= K) i = 0;
            return out;
        }
    };

    inline void processSpring (float xL, float xR, float& yL, float& yR) noexcept
    {
        // driver valvulado do tanque (DWELL) + transdutor de entrada
        const float drvL = std::tanh (xL * dwell) / std::max (1.0f, dwell * 0.6f);
        const float drvR = std::tanh (xR * dwell) / std::max (1.0f, dwell * 0.6f);
        const float in[2] = { springHp.process (drvL), springHp2.process (drvR) };
        float out[2];
        for (int c = 0; c < 2; ++c)
        {
            springDl[c].push (in[c] + springFb[c]);
            float v = springDl[c].read (springLen[c] + springWob * springLfo[c].next());
            for (auto& s : springAp[c]) v = s.process (v, springA);   // dispersão: o "boing"
            v = (c == 0 ? springLp : springLp2).process (v);
            springFb[c] = fx::softClip (v * springG);
            out[c] = v;
        }
        // as duas molas do tanque se misturam um pouco
        yL = out[0] + 0.3f * out[1];
        yR = out[1] + 0.3f * out[0];
    }

    void configure (const Settings& s)
    {
        type = juce::jlimit (0, NumTypes - 1, s.type);
        const float p = juce::jlimit (0.0f, 1.0f, s.param / 10.0f);
        const float t = juce::jlimit (0.0f, 1.0f, s.tone / 10.0f);
        const float m = juce::jlimit (0.0f, 1.0f, s.mod / 10.0f);
        const float rt = decaySeconds (type, s.decay);

        const bool changed = type != lastType || std::abs (p - lastP) > 1e-4f || std::abs (t - lastT) > 1e-4f
                             || std::abs (m - lastM) > 1e-4f || std::abs (rt - lastRt) > 1e-4f;
        if (! changed) return;
        if (type != lastType)
        {
            // trocar de algoritmo limpa o tanque (evita estalo com conteúdo do tipo anterior)
            for (auto& l : lines) l.clear();
            shimFb[0] = shimFb[1] = 0.0f;
        }
        lastType = type; lastP = p; lastT = t; lastM = m; lastRt = rt;

        // ---- tamanho, difusão e caráter de cada tipo ----
        float size = 1.0f, diffG = 0.7f, bassRatio = 1.1f;
        erLevel = 0.0f;
        switch (type)
        {
            case Room:     size = 0.28f + 0.42f * p; diffG = 0.72f; bassRatio = 1.0f; erLevel = 0.55f; break;
            case Hall:     size = 0.85f + 0.8f * p;  diffG = 0.7f;  bassRatio = 1.3f; erLevel = 0.30f; break;
            case Shimmer:
            case Shimmer5: size = 1.4f;              diffG = 0.75f; bassRatio = 1.0f; break;
            case Bloom:    size = 1.3f;              diffG = 0.72f; bassRatio = 1.2f; break;
            case Cloud:    size = 1.9f;              diffG = 0.62f + 0.13f * p; bassRatio = 1.25f; break;
            case Swell:    size = 1.2f;              diffG = 0.7f;  bassRatio = 1.2f; erLevel = 0.15f; break;
            case Gated:    size = 0.45f;             diffG = 0.75f; bassRatio = 1.0f; erLevel = 0.4f; break;
            default: break;
        }
        for (int c = 0; c < 2; ++c)
            for (auto& a : diff[c]) a.g = diffG;

        // 16 linhas com tamanhos sem múltiplos comuns, espalhados de forma exponencial
        static const double baseMs[16] = { 23.17, 26.51, 29.39, 32.83, 36.07, 39.61, 43.39, 47.53,
                                           51.71, 56.29, 61.07, 66.31, 71.83, 77.57, 83.89, 90.53 };
        // agudos decaem mais rápido (TONE), graves um pouco mais lentos (bassRatio)
        const float hiRatio = 0.12f + 0.78f * t;
        const double fc = 1200.0 * std::pow (5.0, (double) t);                    // 1,2 .. 6 kHz
        for (int k = 0; k < 16; ++k)
        {
            lineLen[k] = std::min ((double) lines[(size_t) k].mask - 64.0, baseMs[k] * 0.001 * fs * size);
            const double secs = lineLen[k] / fs;
            const float gMid = (float) std::pow (10.0, -3.0 * secs / rt);
            const float gLo = (float) std::pow (10.0, -3.0 * secs / (rt * bassRatio));
            const float gHi = (float) std::pow (10.0, -3.0 * secs / (rt * hiRatio));
            juce::ignoreUnused (gMid);
            damp[(size_t) k].set (fs, fc, gLo, gHi);
            lfo[(size_t) k].setRate (fs, 0.13 + 0.05 * k * (0.7 + 0.6 * m));
        }
        // profundidade da modulação (amostras): leve nas salas, forte nos ambientes
        const double modScale = (type == Cloud || type == Shimmer || type == Shimmer5 || type == Bloom) ? 2.0 : (type == Room || type == Gated ? 0.6 : 1.0);
        modDepth = (0.3 + 12.0 * m) * modScale * (fs / 48000.0);
        cloudMod = (2.0 + 25.0 * m) * (fs / 48000.0);

        // Reflexões iniciais: 12 ecos por lado, mais espaçados quanto maior a sala
        {
            static const float erMs[12] = { 3.1f, 5.3f, 7.9f, 10.7f, 13.9f, 17.3f, 21.1f, 25.9f, 31.3f, 37.1f, 43.7f, 51.1f };
            const float erSize = type == Hall ? 1.6f + 0.8f * p : (type == Room ? 0.5f + 0.9f * p : 1.0f);
            for (int k = 0; k < 12; ++k)
            {
                erTapL[k] = (int) (erMs[k] * erSize * 0.001f * (float) fs);
                erTapR[k] = (int) (erMs[k] * erSize * 1.07f * 0.001f * (float) fs);
                erGain[k] = (k & 1 ? -1.0f : 1.0f) * std::pow (0.82f, (float) k) * 0.45f;
            }
        }

        // Bloom: allpasses longos em série (o som "abre" devagar); BLOOM = comprimento
        {
            static const double bms[6] = { 53.0, 71.0, 89.0, 107.0, 127.0, 151.0 };
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < 6; ++k)
                {
                    auto& a = bloomAp[c][(size_t) k];
                    a.len = std::min (0.19 * fs, bms[k] * (0.35 + 0.85 * p) * (c == 0 ? 1.0 : 1.043) * 0.001 * fs);
                    a.g = 0.62f + 0.12f * p;
                }
        }
        // Cloud: difusores modulados (DIFFUSION = densidade e tamanho)
        {
            static const double cms[8] = { 7.1, 11.3, 17.9, 23.7, 31.3, 41.9, 53.3, 67.1 };
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < 8; ++k)
                {
                    auto& a = cloudAp[c][(size_t) k];
                    a.len = std::min (0.11 * fs, cms[k] * (0.5 + 0.9 * p) * (c == 0 ? 1.0 : 1.061) * 0.001 * fs);
                    a.g = 0.55f + 0.2f * p;
                }
        }

        // Shimmer: AMOUNT = quanto do som transposto volta para o tanque
        shimAmt = 0.15f + 0.5f * p;
        shimOut = 0.15f + 0.35f * p;
        shimHp.highpass (fs, 500.0, 0.7);  shimHp2.highpass (fs, 500.0, 0.7);
        const double shimTop = 4000.0 + 8000.0 * t;
        shimLp.lowpass (fs, shimTop, 0.6); shimLp2.lowpass (fs, shimTop, 0.6);

        // Plate (Dattorro): decaimento pelo tempo de volta completa do tanque
        plDecay = (float) std::min (0.9995, std::pow (10.0, -3.0 * plLoop / (4.0 * rt)));
        plDamp = (float) std::exp (-2.0 * kPi * (2500.0 * std::pow (6.0, (double) t)) / fs);
        plBw = (float) std::exp (-2.0 * kPi * 12000.0 / fs);
        plModDepth = (2.0 + 22.0 * m) * (fs / 29761.0);
        plLfo0.setRate (fs, 0.9); plLfo1.setRate (fs, 0.67);

        // Spring
        dwell = 1.0f + 6.0f * p;
        springLen[0] = 0.0373 * fs; springLen[1] = 0.0431 * fs;
        // o tempo de volta real inclui o atraso da cascata dispersiva (medido: ~1,6x a linha)
        springG = (float) std::pow (10.0, -3.0 * 1.6 * (springLen[0] / fs) / rt);
        springA = 0.55f;
        springWob = (0.5 + 4.0 * m) * (fs / 48000.0);
        springLfo[0].setRate (fs, 0.7); springLfo[1].setRate (fs, 0.9);
        springHp.highpass (fs, 180.0, 0.7); springHp2.highpass (fs, 180.0, 0.7);
        const double springTop = juce::jmin (6000.0, 2500.0 + 4000.0 * t);
        springLp.lowpass (fs, springTop, 0.9); springLp2.lowpass (fs, springTop, 0.9);

        swellRate = 4.0f / ((0.1f + 2.5f * p * p) * (float) fs);
        gateTime = (int) ((0.12f + 0.6f * p) * (float) fs);

        // EQ de saída
        const double lowCut = type == Plate ? 40.0 + 360.0 * (1.0 - p) : (type == Spring ? 160.0 : 60.0);
        for (auto* b : { &hpL, &hpR }) b->highpass (fs, lowCut, 0.6);
        for (auto* b : { &hp2L, &hp2R }) b->highpass (fs, lowCut, 0.6);
        const double hi = 3500.0 * std::pow (5.0, (double) t);    // 3,5 .. 17,5 kHz
        for (auto* b : { &lpL, &lpR }) b->lowpass (fs, hi, 0.6);
        for (auto* b : { &lp2L, &lp2R }) b->lowpass (fs, hi, 0.6);

        // volume do molhado igualado entre os tipos (medido)
        static const float scale[NumTypes] = { 1.29f, 1.06f, 1.82f, 0.82f, 0.89f, 0.89f, 0.89f, 0.88f, 3.2f, 0.97f };
        outScale = scale[type];

        // sinais de entrada/saída das linhas (padrões ortogonais)
        for (int k = 0; k < 16; ++k)
        {
            inSign[k] = ((k * 5 + 3) % 7) < 3 ? -1.0f : 1.0f;
            auto parity = [] (int v) { int c = 0; while (v) { c ^= (v & 1); v >>= 1; } return c; };
            outSignL[k] = parity (k & 0b0101) ? -1.0f : 1.0f;
            outSignR[k] = parity (k & 0b0011) ? -1.0f : 1.0f;
        }
    }

    double fs = 48000;
    int type = Hall, lastType = -1;
    float lastP = -1, lastT = -1, lastM = -1, lastRt = -1;

    // FDN
    std::array<fx::DelayLine, 16> lines;
    std::array<fx::DecayFilter, 16> damp;
    std::array<fx::SmoothRandom, 16> lfo;
    double lineLen[16] {}, modDepth = 2.0, cloudMod = 5.0, curPreD = 1.0;
    std::array<fx::SmoothRandom, 8> cloudLfo;
    float inSign[16] {}, outSignL[16] {}, outSignR[16] {};
    fx::DelayLine preL, preR;
    std::array<fx::Allpass, 4> diff[2];
    std::array<fx::Allpass, 6> bloomAp[2];
    std::array<fx::Allpass, 8> cloudAp[2];
    int erTapL[12] {}, erTapR[12] {}; float erGain[12] {}, erLevel = 0.0f;

    // Shimmer
    fx::SpectralShifter shimOct[2], shim5[2];
    float shimFb[2] {}, shimAmt = 0.4f, shimOut = 0.3f;
    Biquad shimHp, shimLp, shimHp2, shimLp2;

    // Plate
    std::array<fx::Allpass, 4> plIn;
    fx::Allpass plAp1[2], plAp2[2];
    fx::DelayLine plD1[2], plD2[2];
    double plLen[2] {}, plLen2[2] {}, plLoop = 1.0, plModDepth = 8.0;
    float plDecay = 0.5f, plDamp = 0.3f, plBw = 0.1f, plBwZ = 0.0f, plDampZ[2] {}, plFeed[2] {};
    fx::SineLfo plLfo0, plLfo1;

    // Spring
    static constexpr int springK = 4;
    fx::DelayLine springDl[2];
    std::array<StretchedAp, 48> springAp[2];
    double springLen[2] { 1800, 2000 }, springWob = 1.0;
    float springG = 0.5f, springA = 0.55f, springFb[2] {}, dwell = 2.0f;
    fx::SmoothRandom springLfo[2] { fx::SmoothRandom (5u), fx::SmoothRandom (9u) };
    Biquad springHp, springLp, springHp2, springLp2;

    float swellRate = 0.001f, outScale = 1.0f;
    int gateTime = 10000, gateHold = 0;
    float env = 0, envSlow = 0, swellGain = 1, gateGain = 0;
    float inputGain = 0, wetGain = 0, dryGain = 1;
    bool idle = true; int quietSamples = 0;
    Biquad hpL, hpR, lpL, lpR, hp2L, hp2R, lp2L, lp2R;
};
} // namespace vlv

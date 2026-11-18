#pragma once
// Redutor de ruído (não é um gate): limpa o chiado/zumbido da guitarra ANTES do amp, porque o
// amp aumenta o ruído junto com o som. Funciona também enquanto você toca.
//  - O sinal é dividido em 8 faixas de frequência (banco de filtros que soma exatamente o
//    sinal original: com o knob em 0 o som sai idêntico, sem atraso).
//  - Em cada faixa, o plugin aprende sozinho o nível do ruído de fundo (o nível mínimo que a
//    faixa atinge nos últimos segundos, sempre se atualizando).
//  - A faixa só é abaixada quando o que tem nela está perto do nível do ruído. Quando há nota
//    tocando naquela faixa, ela passa intacta. Por isso o chiado agudo some mesmo enquanto
//    uma nota grave soa, e as notas não são cortadas como num gate.
//  - Atenuação máxima pelo knob (0 = desligado, 10 = até -30 dB no ruído).
#include <array>
#include <cmath>
#include <algorithm>
#include "Biquad.h"

namespace vlv
{
class Denoiser
{
public:
    static constexpr int NB = 8;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        static const double xo[NB - 1] = { 160.0, 320.0, 640.0, 1250.0, 2500.0, 5000.0, 9000.0 };
        for (int k = 0; k < NB - 1; ++k) { lp[k].lowpass (fs, std::min (xo[k], 0.45 * fs), 0.707); lp[k].reset(); }
        // detector: sobe em ~1 ms, desce em ~25 ms
        envUp = (float) std::exp (-1.0 / (0.001 * fs));
        envDn = (float) std::exp (-1.0 / (0.025 * fs));
        // média lenta (~60 ms) usada só para medir o ruído de fundo
        slowC = (float) std::exp (-1.0 / (0.060 * fs));
        // média de ~8 ms (estável no chiado) para a decisão
        medC = (float) std::exp (-1.0 / (0.008 * fs));
        // ganho: abre em ~0,5 ms (não come o ataque da palhetada), fecha em ~45 ms (suave)
        gOpen  = (float) std::exp (-1.0 / (0.0005 * fs));
        gClose = (float) std::exp (-1.0 / (0.045 * fs));
        // estimativa de ruído: mínimo dos últimos ~8 s (janelas de 1 s); o nível estimado só sobe devagar
        winLen = (int) (1.0 * fs);
        riseCoef = (float) std::pow (10.0, 10.0 / 10.0 / fs);     // potência: +10 dB por segundo
        reset();
    }

    void reset()
    {
        for (int k = 0; k < NB; ++k)
        {
            env[k] = 0.0f; slow[k] = 0.0f; med[k] = 0.0f; gain[k] = 1.0f; floorP[k] = 1e-10f; winMin[k] = 1e30f;
            for (auto& h : hist[k]) h = 1e30f;
        }
        for (auto& f : lp) f.reset();
        winCount = 0;
    }

    // amount: 0..10
    void setAmount (float a)
    {
        amount = std::clamp (a, 0.0f, 10.0f) / 10.0f;
        // atenuação máxima: 0 dB .. -30 dB
        floorGain = std::pow (10.0f, -30.0f * amount / 20.0f);
    }

    void process (float* x, int n) noexcept
    {
        if (amount <= 0.0f && ! wasOn) return;
        for (int i = 0; i < n; ++i)
        {
            const float in = x[i];
            // banco de filtros: diferenças de passa-baixas em paralelo -> soma telescópica = entrada
            float lps[NB - 1];
            for (int k = 0; k < NB - 1; ++k) lps[k] = lp[k].process (in);
            float band[NB];
            band[0] = lps[0];
            for (int k = 1; k < NB - 1; ++k) band[k] = lps[k] - lps[k - 1];
            band[NB - 1] = in - lps[NB - 2];

            float y = 0.0f;
            for (int k = 0; k < NB; ++k)
            {
                const float p = band[k] * band[k];
                env[k] = p + (p > env[k] ? envUp : envDn) * (env[k] - p);
                slow[k] = p + slowC * (slow[k] - p);
                med[k] = p + medC * (med[k] - p);
                // ruído de fundo: mínimo da média lenta na janela atual e na anterior
                winMin[k] = std::min (winMin[k], slow[k]);
                float m = winMin[k];
                for (float h : hist[k]) m = std::min (m, h);
                // desce na hora, sobe devagar; nunca passa de -55 dBFS (ruído real nunca é tão alto:
                // isso evita confundir uma nota longa com ruído quando você toca sem pausas)
                floorP[k] = std::clamp (std::min (floorP[k] * riseCoef, m), 1e-12f, maxFloor);

                // Expansor por faixa: referência ~8 dB acima do ruído de fundo medido.
                // Abaixo da referência a faixa desce 2,5 dB para cada 1 dB (curva suave, sem "corte").
                // Detector de decisão: média de ~8 ms (estável no chiado) ou, num ataque de
                // palhetada, o detector rápido (com 6 dB de folga para os picos do próprio chiado)
                const float det = std::max (med[k], env[k] * 0.25f);
                const float ref = floorP[k] * 6.0f;
                float target = 1.0f;
                if (det < ref)
                {
                    const float r = det / ref;             // razão de potência (< 1)
                    target = std::max (floorGain, r * std::sqrt (r));   // expansão 1:2,5 em dB
                }
                target = 1.0f + (target - 1.0f) * std::min (1.0f, amount * 2.0f);   // knob baixo = efeito mais leve
                const float c = target > gain[k] ? gOpen : gClose;
                gain[k] = target + c * (gain[k] - target);
                y += band[k] * gain[k];
            }
            x[i] = y;
        }
        winCount += n;
        if (winCount >= winLen)
        {
            winCount = 0;
            histPos = (histPos + 1) % NH;
            for (int k = 0; k < NB; ++k) { hist[k][(size_t) histPos] = winMin[k]; winMin[k] = 1e30f; }
        }
        wasOn = amount > 0.0f;
    }

private:
    double fs = 48000;
    std::array<Biquad, NB - 1> lp;
    static constexpr int NH = 8;                    // lembra o mínimo dos últimos 8 segundos
    float env[NB] {}, slow[NB] {}, med[NB] {}, gain[NB] {}, floorP[NB] {}, winMin[NB] {};
    std::array<float, NH> hist[NB];
    int histPos = 0;
    static constexpr float maxFloor = 3.2e-6f;    // -55 dB (potência)
    float envUp = 0.99f, envDn = 0.999f, slowC = 0.999f, medC = 0.99f, gOpen = 0.99f, gClose = 0.999f, riseCoef = 1.0f;
    float amount = 0.0f, floorGain = 1.0f;
    int winLen = 28800, winCount = 0;
    bool wasOn = false;
};
} // namespace vlv

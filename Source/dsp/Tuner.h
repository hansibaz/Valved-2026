#pragma once
// Afinador de alta precisão.
//  - O áudio da guitarra (logo depois do Input, antes de tudo) vai para uma fila sem trava;
//    a análise roda na interface (fora do áudio), ~30 vezes por segundo.
//  - Detecção: método de McLeod (NSDF, "McLeod Pitch Method"), com autocorrelação por FFT,
//    escolha do primeiro pico forte (não confunde com harmônicos/oitavas) e interpolação
//    parabólica do pico -> precisão de centésimos de cent em notas limpas.
//  - Refinamento: uma segunda análise com janela longa centrada no período encontrado
//    (média de vários ciclos), mediana das últimas leituras e suavização que só age quando
//    a nota está parada (resposta rápida quando você gira a tarraxa).
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <array>
#include <atomic>
#include <cmath>
#include <algorithm>

namespace vlv
{
// Fila de áudio sem trava (o áudio escreve, a interface lê as últimas N amostras)
class TunerFeed
{
public:
    static constexpr int size = 1 << 15;
    void push (const float* x, int n) noexcept
    {
        int w = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) { buf[(size_t) w] = x[i]; w = (w + 1) & (size - 1); }
        writePos.store (w, std::memory_order_release);
    }
    // copia as últimas n amostras (n <= size)
    void latest (float* out, int n) const noexcept
    {
        const int w = writePos.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i) out[i] = buf[(size_t) ((w - n + i) & (size - 1))];
    }
    std::atomic<double> sampleRate { 48000.0 };
private:
    std::array<float, size> buf {};
    std::atomic<int> writePos { 0 };
};

class TunerAnalyzer
{
public:
    struct Reading
    {
        bool valid = false;
        double freq = 0.0;     // Hz
        int midi = 0;          // nota mais próxima
        double cents = 0.0;    // desvio da nota (-50..+50)
        float clarity = 0.0f;  // 0..1 (confiança)
        float level = 0.0f;    // nível do sinal
    };

    TunerAnalyzer() { win.assign ((size_t) W, 0.0f); data.assign ((size_t) (4 * W), 0.0f); }

    Reading analyse (const TunerFeed& feed, double refA4)
    {
        const double fs = feed.sampleRate.load();
        feed.latest (win.data(), W);

        Reading r;
        double e = 0.0; float pk = 0.0f;
        for (int i = 0; i < W; ++i) { e += (double) win[(size_t) i] * win[(size_t) i]; pk = std::max (pk, std::abs (win[(size_t) i])); }
        r.level = pk;
        if (pk < 0.003f) { hist.clear(); smooth = 0.0; return r; }      // silêncio

        // remove DC
        double mean = 0.0; for (auto v : win) mean += v; mean /= W;
        for (auto& v : win) v -= (float) mean;

        double period = 0.0; float clarity = 0.0f;
        if (! mpm (win.data(), W, fs, period, clarity)) { hist.clear(); return r; }
        r.clarity = clarity;
        if (clarity < 0.80f) { hist.clear(); return r; }

        double f = fs / period;
        if (f < 25.0 || f > 2000.0) return r;

        // mediana das últimas leituras (descarta saltos isolados) + suavização quando parada
        hist.push_back (f);
        if (hist.size() > 5) hist.erase (hist.begin());
        auto sorted = hist; std::sort (sorted.begin(), sorted.end());
        const double med = sorted[sorted.size() / 2];
        const double centsJump = 1200.0 * std::log2 (med / std::max (1e-9, smooth > 0 ? smooth : med));
        if (smooth <= 0.0 || std::abs (centsJump) > 3.0) smooth = med;           // mudou de nota/afinação: segue na hora
        else smooth += 0.35 * (med - smooth);                                    // parada: estabiliza
        f = smooth;

        const double midiF = 69.0 + 12.0 * std::log2 (f / refA4);
        r.midi = (int) std::lround (midiF);
        r.cents = 100.0 * (midiF - r.midi);
        r.freq = f;
        r.valid = true;
        return r;
    }

private:
    static constexpr int order = 13, W = 1 << order;   // 8192 amostras (~170 ms a 48 kHz): pega até o Si grave de 7 cordas

    // McLeod Pitch Method
    bool mpm (const float* x, int n, double fs, double& period, float& clarity)
    {
        // autocorrelação por FFT (zero-padding para 2n)
        std::fill (data.begin(), data.end(), 0.0f);
        for (int i = 0; i < n; ++i) data[(size_t) i] = x[i];
        bigFft.performRealOnlyForwardTransform (data.data(), true);
        for (int k = 0; k <= n; ++k)
        {
            const float re = data[(size_t) (2 * k)], im = data[(size_t) (2 * k + 1)];
            data[(size_t) (2 * k)] = re * re + im * im; data[(size_t) (2 * k + 1)] = 0.0f;
        }
        bigFft.performRealOnlyInverseTransform (data.data());
        // NSDF: n(t) = 2 r(t) / m(t),  m(t) = soma de x[j]^2 + x[j+t]^2
        const int maxLag = std::min (n / 2, (int) (fs / 25.0));
        const int minLag = std::max (2, (int) (fs / 2000.0));
        nsdf.assign ((size_t) maxLag, 0.0f);
        double m = 0.0; for (int i = 0; i < n; ++i) m += 2.0 * (double) x[i] * x[i];
        for (int t = 0; t < maxLag; ++t)
        {
            if (t > 0) m -= (double) x[t - 1] * x[t - 1] + (double) x[n - t] * x[n - t];
            nsdf[(size_t) t] = m > 1e-12 ? (float) (2.0 * data[(size_t) t] / m) : 0.0f;
        }
        // picos-chave: o maior entre cada cruzamento positivo e o próximo negativo
        std::vector<int> peaks;
        int t = minLag;
        while (t < maxLag && nsdf[(size_t) t] > 0.0f) ++t;       // sai do lóbulo do atraso zero
        while (t < maxLag - 1)
        {
            while (t < maxLag - 1 && nsdf[(size_t) t] <= 0.0f) ++t;
            int best = -1;
            while (t < maxLag - 1 && nsdf[(size_t) t] > 0.0f)
            {
                if (best < 0 || nsdf[(size_t) t] > nsdf[(size_t) best]) best = t;
                ++t;
            }
            if (best > 0) peaks.push_back (best);
        }
        if (peaks.empty()) return false;
        float top = 0.0f; for (int p : peaks) top = std::max (top, nsdf[(size_t) p]);
        const float thr = 0.92f * top;
        int chosen = peaks.back();
        for (int p : peaks) if (nsdf[(size_t) p] >= thr) { chosen = p; break; }
        // interpolação parabólica
        const double a = nsdf[(size_t) chosen - 1], b = nsdf[(size_t) chosen], c = nsdf[(size_t) chosen + 1];
        const double den = a - 2.0 * b + c;
        const double off = std::abs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0;
        period = chosen + juce::jlimit (-0.5, 0.5, off);
        clarity = (float) (b - 0.25 * (a - c) * off);

        // refinamento: período médio de vários ciclos (pico do NSDF no múltiplo k do período)
        const int k = (int) ((maxLag - 2) / period);
        if (k >= 2)
        {
            const int lag = (int) std::lround (k * period);
            if (lag + 1 < maxLag)
            {
                int bestL = lag;
                for (int d = -2; d <= 2; ++d) if (nsdf[(size_t) (lag + d)] > nsdf[(size_t) bestL]) bestL = lag + d;
                const double a2 = nsdf[(size_t) bestL - 1], b2 = nsdf[(size_t) bestL], c2 = nsdf[(size_t) bestL + 1];
                const double den2 = a2 - 2.0 * b2 + c2;
                const double off2 = std::abs (den2) > 1e-12 ? 0.5 * (a2 - c2) / den2 : 0.0;
                if (b2 > 0.8 * b)
                {
                    const double p2 = (bestL + juce::jlimit (-0.5, 0.5, off2)) / k;
                    if (std::abs (p2 - period) < 0.02 * period) period = p2;
                }
            }
        }
        return true;
    }

    juce::dsp::FFT bigFft { order + 1 };
    std::vector<float> win, data, nsdf;
    std::vector<double> hist;
    double smooth = 0.0;
};
} // namespace vlv

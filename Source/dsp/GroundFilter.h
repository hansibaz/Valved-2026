#pragma once
// GROUND FILTER: remove só as interferências elétricas que chegam junto com a guitarra pela
// interface de áudio, sem mexer no resto do som (não é gate nem redutor de chiado):
//  - Zumbido da rede elétrica (50 ou 60 Hz e todos os harmônicos), típico de guitarra sem
//    blindagem ou de loop de terra.
//  - Apito da USB (linhas em 1 kHz, 2 kHz, 3 kHz...), típico de interfaces USB.
//  - Outras linhas fixas de equipamentos (fonte chaveada, monitor, etc.).
// Como funciona:
//  - Nas pausas (quando você não está tocando), o plugin analisa o ruído em duas FFTs longas
//    seguidas (~1,4 s cada, resolução de ~0,7 Hz) e acha cada linha estreita e CONSTANTE
//    (presente nas duas, com o mesmo nível). Uma corda soando sozinha não entra, porque o nível
//    dela cai com o tempo. A análise roda fora da thread de áudio.
//  - No áudio, cada linha encontrada recebe um filtro "notch" estreitíssimo (3 a 5 Hz de
//    largura) na frequência exata. As notas da guitarra passam praticamente intactas.
//  - Se nada for encontrado, nenhum filtro é aplicado.
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <array>
#include <atomic>
#include <cmath>
#include <algorithm>

namespace vlv
{
class GroundFilter
{
public:
    static constexpr int MaxNotches = 96;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        order = 15;
        while ((1 << order) < (int) (fs * 1.3)) ++order;
        N = 1 << order;
        ring.assign ((size_t) (4 * N), 0.0f);
        w.store (0);
        fft = std::make_unique<juce::dsp::FFT> (order);
        work.assign ((size_t) (2 * N), 0.0f);
        win.resize ((size_t) N);
        for (int i = 0; i < N; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / N));
        for (auto& s : st) s = {};
        active = 0; applied = -1;
        mix = enabled ? 1.0f : 0.0f;
        mixStep = (float) (1.0 / (0.02 * fs));
        // a análise anterior continua valendo (mesma taxa) — recalcula os coeficientes
        applied = -1;
    }

    void setEnabled (bool e) { enabled = e; }
    // esquece o que aprendeu (ex.: trocou de guitarra/lugar)
    void forget() { cands.clear(); Set empty; const int next = version.load() + 1; sets[(size_t) (next & 1)] = empty; version.store (next); humLines = 0; usbLines = 0; learned = false; }

    // ---- thread de áudio
    void process (float* x, int n) noexcept
    {
        // guarda o sinal para a análise (mesmo desligado, para já saber o ruído quando ligar)
        int wp = w.load (std::memory_order_relaxed);
        const int mask = (int) ring.size() - 1;
        for (int i = 0; i < n; ++i) { ring[(size_t) wp] = x[i]; wp = (wp + 1) & mask; }
        w.store (wp, std::memory_order_release);

        // novos filtros publicados pela análise?
        const int ver = version.load (std::memory_order_acquire);
        if (ver != applied)
        {
            applied = ver;
            const auto& set = sets[(size_t) (ver & 1)];
            active = set.count;
            for (int k = 0; k < active; ++k) design (k, set.freq[(size_t) k], set.bw[(size_t) k]);   // mesmos lugares: os antigos não mudam
        }

        const float target = enabled ? 1.0f : 0.0f;
        if (active == 0 || (target == 0.0f && mix == 0.0f)) { mix = target == 0.0f ? 0.0f : mix; return; }
        for (int i = 0; i < n; ++i)
        {
            double y = x[i];
            for (int k = 0; k < active; ++k)
            {
                auto& s = st[(size_t) k]; const auto& c = co[(size_t) k];
                const double o = c.b0 * y + s.z1;
                s.z1 = c.b1 * y - c.a1 * o + s.z2;
                s.z2 = c.b2 * y - c.a2 * o;
                y = o;
            }
            if (mix != target) mix = target > mix ? std::min (target, mix + mixStep) : std::max (target, mix - mixStep);
            x[i] = x[i] + ((float) y - x[i]) * mix;
        }
    }

    // ---- fora do áudio (timer): analisa quando a guitarra está em silêncio
    void analyse()
    {
        if (N == 0) return;
        const int mask = (int) ring.size() - 1;
        const int wp = w.load (std::memory_order_acquire);
        // duas janelas seguidas de N amostras (a mais antiga e a mais recente)
        double e = 0.0; float pk = 0.0f;
        for (int i = 0; i < 2 * N; ++i)
        {
            const float v = ring[(size_t) ((wp - 2 * N + i) & mask)];
            e += (double) v * v; pk = std::max (pk, std::abs (v));
        }
        const double rmsDb = 10.0 * std::log10 (e / (2 * N) + 1e-30);
        // só analisa pausas: sem notas (nível baixo e sem picos de palhetada)
        if (rmsDb > -42.0 || pk > 0.04f || rmsDb < -130.0) { quietNow = false; return; }
        quietNow = true;
        quietAnalyses.store (quietAnalyses.load() + 1);

        const int half = N / 2;
        const double df = fs / N;
        std::vector<float> PA ((size_t) half), PB ((size_t) half);
        for (int part = 0; part < 2; ++part)
        {
            for (int i = 0; i < N; ++i) work[(size_t) i] = ring[(size_t) ((wp - 2 * N + part * N + i) & mask)] * win[(size_t) i];
            std::fill (work.begin() + N, work.end(), 0.0f);
            fft->performFrequencyOnlyForwardTransform (work.data());
            auto& P = part == 0 ? PA : PB;
            for (int k = 0; k < half; ++k) P[(size_t) k] = work[(size_t) k] * work[(size_t) k] + 1e-30f;
        }
        // piso local (mediana de ±12 Hz)
        const int rad = std::max (8, (int) (12.0 / df));
        auto localFloor = [&] (const std::vector<float>& P, int k)
        {
            std::vector<float> v;
            for (int j = std::max (1, k - rad); j < std::min (half, k + rad); ++j) v.push_back (P[(size_t) j]);
            std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
            return v[v.size() / 2];
        };
        struct Line { double f, snr, level; };
        std::vector<Line> lines;
        const int k0 = (int) (40.0 / df), k1 = (int) (std::min (5500.0, 0.45 * fs) / df);   // acima disso a caixa já corta
        for (int k = std::max (3, k0); k < k1; ++k)
        {
            const float a = PA[(size_t) k];
            if (! (a > PA[(size_t) k - 1] && a >= PA[(size_t) k + 1])) continue;             // máximo local
            if (a < 6.0f * std::max (PA[(size_t) k - 3], PA[(size_t) k + 3])) continue;         // estreito
            const double snrA = 10.0 * std::log10 (a / localFloor (PA, k));
            if (snrA < 12.0) continue;
            // a mesma linha na segunda janela, com o mesmo nível (constante = interferência)
            int kb = k; for (int j = k - 1; j <= k + 1; ++j) if (PB[(size_t) j] > PB[(size_t) kb]) kb = j;
            const double snrB = 10.0 * std::log10 (PB[(size_t) kb] / localFloor (PB, kb));
            if (snrB < 10.0 || std::abs (10.0 * std::log10 (PB[(size_t) kb] / a)) > 6.0) continue;
            // frequência exata (interpolação parabólica no log)
            auto interp = [&] (const std::vector<float>& P, int c)
            {
                const double l = std::log (P[(size_t) c - 1]), m = std::log (P[(size_t) c]), r = std::log (P[(size_t) c + 1]);
                const double den = l - 2.0 * m + r;
                return (c + (std::abs (den) > 1e-12 ? juce::jlimit (-0.5, 0.5, 0.5 * (l - r) / den) : 0.0)) * df;
            };
            lines.push_back ({ 0.5 * (interp (PA, k) + interp (PB, kb)), 0.5 * (snrA + snrB), 10.0 * std::log10 (PB[(size_t) kb]) });
        }
        std::sort (lines.begin(), lines.end(), [] (const Line& x, const Line& y) { return x.snr > y.snr; });

        // Confirmação: uma linha só vira filtro depois de aparecer em várias análises sem perder
        // nível (uma corda soando decai; interferência elétrica fica constante).
        // Os filtros já ativos nunca mudam de lugar (só se acrescentam novos), para não dar estalo.
        for (const auto& l : lines)
        {
            bool found = false;
            for (auto& c : cands)
                if (std::abs (c.f - l.f) < 1.0)
                {
                    c.f = 0.5 * (c.f + l.f); ++c.hits; found = true;
                    if (l.level < c.level0 - 6.0) c.decaying = true;
                    break;
                }
            if (! found && cands.size() < 400) cands.push_back ({ l.f, 1, false, l.level, false });
        }
        // frequência exata da rede (50 ou 60 Hz) a partir das linhas encontradas
        double mains = 0.0; int bestN = 0;
        for (double base : { 50.0, 60.0 })
        {
            std::vector<double> est;
            for (const auto& c : cands)
            {
                const double h = std::round (c.f / base);
                if (h >= 2 && std::abs (c.f - h * base) < 0.006 * h * base + 0.3) est.push_back (c.f / h);
            }
            if ((int) est.size() > bestN && est.size() >= 4)
            {
                bestN = (int) est.size();
                std::sort (est.begin(), est.end());
                mains = est[est.size() / 2];
            }
        }
        auto onGrid = [&] (double f)
        {
            if (mains > 0.0) { const double h = std::round (f / mains); if (h >= 1 && std::abs (f - h * mains) < 0.6) return true; }
            return std::abs (f - 1000.0 * std::round (f / 1000.0)) < 15.0 && f > 500.0;     // apito USB
        };
        Set set = sets[(size_t) (version.load() & 1)];
        bool changed = false;
        // com a rede identificada, os primeiros harmônicos (60-480 Hz) entram sempre: no grave o
        // "piso" do ruído é alto e eles nem sempre se destacam na análise, mas são o zumbido principal
        if (mains > 0.0)
            for (int h = 1; h <= 8; ++h)
            {
                const double f = h * mains;
                bool dup = false;
                for (int k = 0; k < set.count; ++k) if (std::abs (set.freq[(size_t) k] - f) < 1.5) dup = true;
                if (dup || set.count >= MaxNotches) continue;
                // só se houver energia ali acima do ruído vizinho (na janela mais recente)
                const int kk = (int) std::lround (f / df);
                float pk = 0.0f; for (int j = kk - 2; j <= kk + 2; ++j) pk = std::max (pk, PB[(size_t) j]);
                if (10.0 * std::log10 (pk / localFloor (PB, kk)) < 6.0) continue;
                set.freq[(size_t) set.count] = f;
                set.bw[(size_t) set.count] = 1.5;
                ++set.count; changed = true;
            }
        for (auto& c : cands)
        {
            if (c.used || c.decaying || set.count >= MaxNotches) continue;
            const bool ok = onGrid (c.f) ? c.hits >= 2 : c.hits >= 6;     // fora da grade: precisa ser bem mais constante
            if (! ok) continue;
            bool dup = false;
            for (int k = 0; k < set.count; ++k) if (std::abs (set.freq[(size_t) k] - c.f) < 1.5) dup = true;
            c.used = true;
            if (dup) continue;
            set.freq[(size_t) set.count] = c.f;
            set.bw[(size_t) set.count] = juce::jlimit (1.5, 5.0, 1.5 + 0.0012 * c.f);
            ++set.count; changed = true;
        }
        int hum = 0, usb = 0;
        for (int k = 0; k < set.count; ++k)
        {
            const double f = set.freq[(size_t) k];
            if (std::abs (f - 1000.0 * std::round (f / 1000.0)) < 15.0) ++usb; else ++hum;
        }
        humLines.store (hum);
        usbLines.store (usb);
        if (changed)
        {
            const int next = version.load() + 1;
            sets[(size_t) (next & 1)] = set;
            version.store (next, std::memory_order_release);
        }
        learned.store (true);
    }

    // status para a interface
    std::atomic<int> humLines { 0 };
    std::atomic<int> quietAnalyses { 0 };   // quantas pausas já foram analisadas
    std::atomic<int> usbLines { 0 };
    std::atomic<bool> learned { false };
    bool quietNow = false;

private:
    struct Coef { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct State { double z1 = 0, z2 = 0; };
    struct Set { int count = 0; std::array<double, MaxNotches> freq {}, bw {}; };

    void design (int k, double f, double bw)
    {
        const double w0 = 2.0 * juce::MathConstants<double>::pi * f / fs;
        const double Q = std::max (0.5, f / bw);
        const double alpha = std::sin (w0) / (2.0 * Q), cw = std::cos (w0), a0 = 1.0 + alpha;
        auto& c = co[(size_t) k];
        c.b0 = 1.0 / a0; c.b1 = -2.0 * cw / a0; c.b2 = 1.0 / a0; c.a1 = -2.0 * cw / a0; c.a2 = (1.0 - alpha) / a0;
    }

    double fs = 48000;
    int order = 17, N = 0;
    std::vector<float> ring, work, win;
    std::atomic<int> w { 0 };
    std::unique_ptr<juce::dsp::FFT> fft;
    std::array<Set, 2> sets;
    struct Cand { double f; int hits; bool used; double level0; bool decaying; };
    std::vector<Cand> cands;
    std::atomic<int> version { 0 };
    int applied = -1, active = 0;
    std::array<Coef, MaxNotches> co;
    std::array<State, MaxNotches> st;
    bool enabled = false;
    float mix = 0.0f, mixStep = 0.001f;
};
} // namespace vlv

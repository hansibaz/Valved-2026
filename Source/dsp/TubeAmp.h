#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <memory>
#include <map>
#include <string>
#include <array>
#include "Biquad.h"

// =====================================================================================
// Motor valvulado baseado em circuito.
//  - Cada estágio de pré é um triodo 12AX7 (modelo de Koren) resolvido sobre a reta de
//    carga real (resistor de placa, resistor de catodo, B+), gerando a curva de transferência
//    com a assimetria correta (corte de um lado, condução de grade do outro).
//  - Capacitores de acoplamento, bypass de catodo, divisores e "bright caps" usam os valores
//    dos esquemas e viram filtros calculados analiticamente.
//  - O tone stack (Bass/Mid/Treble) é a função de transferência exata do circuito passivo
//    (fórmula de Yeh & Smith), conferida contra simulação nodal.
//  - Presence e Resonance agem na realimentação negativa (NFB) do power amp, como no amp real.
// =====================================================================================
namespace vlv::tube
{
// ---------- Triodo 12AX7 (Koren) ----------
struct Koren
{
    double mu = 100.0, ex = 1.4, kg1 = 1060.0, kp = 600.0, kvb = 300.0;
    double plateCurrent (double vgk, double vpk) const
    {
        if (vpk <= 0.0) return 0.0;
        const double e1 = vpk / kp * std::log1p (std::exp (juce::jlimit (-60.0, 60.0, kp * (1.0 / mu + vgk / std::sqrt (kvb + vpk * vpk)))));
        return e1 > 0.0 ? 2.0 * std::pow (e1, ex) / kg1 : 0.0;
    }
};

// Condução de grade: quando a grade fica positiva em relação ao catodo, ela "segura" o sinal
inline double gridClamp (double vgk) { return vgk > 0.0 ? vgk / (1.0 + vgk / 0.7) : vgk; }

// Curva de transferência de um estágio (entrada: volts na grade; saída: variação de volts na placa)
class StageTable
{
public:
    StageTable (double bPlus, double rp, double rk, bool bypassed)
    {
        const Koren k;
        auto solve = [&] (double vin, double vkFixed, bool fixedK) -> double
        {
            double lo = 0.0, hi = bPlus / rp;
            for (int it = 0; it < 60; ++it)
            {
                const double ip = 0.5 * (lo + hi);
                const double vk = fixedK ? vkFixed : ip * rk;
                const double vp = bPlus - ip * rp;
                const double f = k.plateCurrent (gridClamp (vin - vk), vp - vk);
                if (f > ip) lo = ip; else hi = ip;
            }
            return 0.5 * (lo + hi);
        };
        // Ponto de operação (DC)
        const double ip0 = solve (0.0, 0.0, false);
        const double vk0 = ip0 * rk;
        vp0 = bPlus - ip0 * rp;
        table.resize (size);
        for (int i = 0; i < size; ++i)
        {
            const double vin = lo + (hi - lo) * (double) i / (double) (size - 1);
            const double ip = solve (vin, vk0, bypassed);
            table[(size_t) i] = (float) ((bPlus - ip * rp) - vp0);
        }
        // Ganho de pequeno sinal (para referência)
        smallGain = (lookup (0.01f) - lookup (-0.01f)) / 0.02f;
    }

    inline float lookup (float vin) const noexcept
    {
        const float x = (vin - (float) lo) * scale;
        if (x <= 0.0f) return table.front();
        if (x >= (float) (size - 1)) return table.back();
        const int i = (int) x;
        const float t = x - (float) i;
        return table[(size_t) i] + (table[(size_t) i + 1] - table[(size_t) i]) * t;
    }

    float smallGain = -60.0f;

private:
    static constexpr int size = 4096;
    static constexpr double lo = -40.0, hi = 20.0;
    const float scale = (float) ((size - 1) / (hi - lo));
    std::vector<float> table;
    double vp0 = 0.0;
};

inline std::shared_ptr<StageTable> getTable (double bPlus, double rp, double rk, bool bypassed)
{
    static std::map<std::tuple<int, int, int, bool>, std::shared_ptr<StageTable>> cache;
    static juce::CriticalSection lock;
    const juce::ScopedLock sl (lock);
    auto key = std::make_tuple ((int) bPlus, (int) rp, (int) rk, bypassed);
    auto it = cache.find (key);
    if (it != cache.end()) return it->second;
    auto t = std::make_shared<StageTable> (bPlus, rp, rk, bypassed);
    cache[key] = t;
    return t;
}

// ---------- Filtros lineares genéricos (analógico -> digital por transformação bilinear) ----------
struct IIR1
{
    double b0 = 1, b1 = 0, a1 = 0, z = 0;
    // H(s) = (n0 + n1 s) / (d0 + d1 s)
    void set (double n0, double n1, double d0, double d1, double fs)
    {
        const double c = 2.0 * fs;
        const double A0 = d0 + d1 * c, A1 = d0 - d1 * c;
        b0 = (n0 + n1 * c) / A0; b1 = (n0 - n1 * c) / A0; a1 = A1 / A0;
    }
    void reset() { z = 0; }
    inline float process (float x) noexcept
    {
        const double y = b0 * x + z;
        z = b1 * x - a1 * y;
        return (float) y;
    }
};

struct IIR2
{
    double b[3] { 1, 0, 0 }, a[3] { 1, 0, 0 }, z1 = 0, z2 = 0;
    // H(s) = (n0 + n1 s + n2 s^2) / (d0 + d1 s + d2 s^2)
    void set (double n0, double n1, double n2, double d0, double d1, double d2, double fs)
    {
        const double c = 2.0 * fs, c2 = c * c;
        const double A0 = d0 + d1 * c + d2 * c2;
        b[0] = (n0 + n1 * c + n2 * c2) / A0; b[1] = (2 * n0 - 2 * n2 * c2) / A0; b[2] = (n0 - n1 * c + n2 * c2) / A0;
        a[1] = (2 * d0 - 2 * d2 * c2) / A0; a[2] = (d0 - d1 * c + d2 * c2) / A0;
    }
    void reset() { z1 = z2 = 0; }
    inline float process (float x) noexcept
    {
        const double y = b[0] * x + z1;
        z1 = b[1] * x - a[1] * y + z2;
        z2 = b[2] * x - a[2] * y;
        return (float) y;
    }
};

// Tone stack TMB (Fender/Marshall/Soldano/Mesa) — função exata do circuito
struct ToneStack
{
    double R1 = 220e3, R2 = 1e6, R3 = 25e3, R4 = 33e3, C1 = 470e-12, C2 = 22e-9, C3 = 22e-9;
    double B[4] {}, A[4] {}, z[3] {};

    void set (double t, double m, double l, double fs)
    {
        t = juce::jlimit (0.0, 1.0, t); m = juce::jlimit (0.0, 1.0, m); l = juce::jlimit (0.0, 1.0, l);
        const double b1 = t*C1*R1 + m*C3*R3 + l*(C1*R2 + C2*R2) + (C1*R3 + C2*R3);
        const double b2 = t*(C1*C2*R1*R4 + C1*C3*R1*R4) - m*m*(C1*C3*R3*R3 + C2*C3*R3*R3)
                        + m*(C1*C3*R1*R3 + C1*C3*R3*R3 + C2*C3*R3*R3) + l*(C1*C2*R1*R2 + C1*C2*R2*R4 + C1*C3*R2*R4)
                        + l*m*(C1*C3*R2*R3 + C2*C3*R2*R3) + (C1*C2*R1*R3 + C1*C2*R3*R4 + C1*C3*R3*R4);
        const double b3 = l*m*(C1*C2*C3*R1*R2*R3 + C1*C2*C3*R2*R3*R4) - m*m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                        + m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4) + t*C1*C2*C3*R1*R3*R4 - t*m*C1*C2*C3*R1*R3*R4
                        + t*l*C1*C2*C3*R1*R2*R4;
        const double a1 = (C1*R1 + C1*R3 + C2*R3 + C2*R4 + C3*R4) + m*C3*R3 + l*(C1*R2 + C2*R2);
        const double a2 = m*(C1*C3*R1*R3 - C2*C3*R3*R4 + C1*C3*R3*R3 + C2*C3*R3*R3) + l*m*(C1*C3*R2*R3 + C2*C3*R2*R3)
                        - m*m*(C1*C3*R3*R3 + C2*C3*R3*R3) + l*(C1*C2*R2*R4 + C1*C2*R1*R2 + C1*C3*R2*R4 + C2*C3*R2*R4)
                        + (C1*C2*R1*R4 + C1*C3*R1*R4 + C1*C2*R3*R4 + C1*C2*R1*R3 + C1*C3*R3*R4 + C2*C3*R3*R4);
        const double a3 = l*m*(C1*C2*C3*R1*R2*R3 + C1*C2*C3*R2*R3*R4) - m*m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                        + m*(C1*C2*C3*R3*R3*R4 + C1*C2*C3*R1*R3*R3 - C1*C2*C3*R1*R3*R4) + l*C1*C2*C3*R1*R2*R4
                        + C1*C2*C3*R1*R3*R4;
        const double c = 2.0 * fs, c2 = c * c, c3 = c2 * c;
        const double Bs[4] = { -b1*c - b2*c2 - b3*c3, -b1*c + b2*c2 + 3*b3*c3, b1*c + b2*c2 - 3*b3*c3, b1*c - b2*c2 + b3*c3 };
        const double As[4] = { -1 - a1*c - a2*c2 - a3*c3, -3 - a1*c + a2*c2 + 3*a3*c3, -3 + a1*c + a2*c2 - 3*a3*c3, -1 + a1*c - a2*c2 + a3*c3 };
        for (int i = 0; i < 4; ++i) { B[i] = Bs[i] / As[0]; A[i] = As[i] / As[0]; }
    }
    void reset() { z[0] = z[1] = z[2] = 0; }
    inline float process (float xf) noexcept
    {
        const double x = xf;
        const double y = B[0] * x + z[0];
        z[0] = B[1] * x - A[1] * y + z[1];
        z[1] = B[2] * x - A[2] * y + z[2];
        z[2] = B[3] * x - A[3] * y;
        return (float) y;
    }
};

// Potenciômetro logarítmico (curva "A")
inline double audioTaper (double x) { x = juce::jlimit (0.0, 1.0, x); return (std::pow (10.0, 2.0 * x) - 1.0) / 99.0; }

// ---------- Descrição do circuito ----------
struct StageSpec
{
    double rp = 100e3, rk = 1.5e3, ck = 0.0;   // ck = 0: catodo sem bypass
    double bPlus = 300.0;
    double plateCap = 0.0;                      // capacitor sobre o resistor de placa (suaviza o "fizz")
    double millerHz = 0.0;                      // passa-baixa de entrada (capacitância Miller + grid stopper)
};

// Rede entre estágios: capacitor de acoplamento -> resistor série (com cap em paralelo) -> resistor para terra
// Se isGainPot: o resistor para terra é o potenciômetro de ganho (com bright cap entre topo e cursor)
struct Interstage
{
    double cc = 22e-9;          // capacitor de acoplamento
    double rs = 0.0, cs = 0.0;  // resistor série e cap em paralelo (0 = sem)
    double rShunt = 1e6;        // resistor para terra / valor do potenciômetro
    bool isGainPot = false;
    double cBright = 0.0;       // bright cap (só para pot)
    double extraGain = 1.0;     // estágio "recuperador" (ex.: driver do tone stack)
};

struct PowerSpec
{
    double rfb = 100e3, rTail = 4.7e3, loopGain = 4.0;   // realimentação negativa (0 = sem NFB)
    double presPot = 22e3, presCap = 0.1e-6;              // rede de presence
    double resPot = 0.0, resCap = 0.0;                    // rede de resonance (0 = não tem)
    double piHeadroom = 2.2;      // volts na entrada do inversor de fase até saturar
    double powerKnee = 1.0;       // dureza da saturação das válvulas de potência
    double asym = 0.06;           // desbalanço do push-pull
    double sag = 0.15;            // queda da fonte com sinal forte
    double loadResHz = 95.0;      // ressonância do alto-falante (reflete na carga)
};

// Banda de EQ fixa (voicing medido do amp real): 0 desligada, 1 pico, 2 shelf grave, 3 shelf agudo,
// 4 passa-baixa 2ª ordem, 5 passa-alta 2ª ordem
struct EqBand { double type = 0, f = 1000.0, q = 0.707, db = 0.0; };

struct CircuitSpec
{
    std::vector<StageSpec> stages;
    std::vector<Interstage> nets;       // nets[i] vem DEPOIS de stages[i]
    bool cathodeFollower = true;
    ToneStack tone;                     // valores dos componentes
    double trebleTaperDb = 0.0, bassTaperDb = 40.0, midTaperDb = 0.0;   // curvas dos pots (0 = linear, 40 = "A")
    double toneRecovery = 1.0;          // ganho do estágio após o tone stack (5150)
    double masterPot = 1e6;
    PowerSpec power;
    double inputScale = 1.0;            // volts por unidade digital na entrada
    double outTrimDb = 0.0;

    // Curvas dos potenciômetros (ajustadas pelas medições)
    double gainLo = 0.0, gainHi = 1.0;  // trecho do pot de gain percorrido pelo knob 0..10
    double gainTaperDb = 40.0;          // curva do pot de gain (40 dB = pot "A" padrão)
    double masterTaperDb = 40.0;
    // Amp transistorizado (sem válvulas de pré): o knob gain é o Volume, antes do tone stack
    double ssClip = 0.0;                // nível de saturação do pré transistorizado (0 = sem)
    double cfPos = 45.0, cfNeg = 90.0;  // limites do seguidor de catodo
    double loadResDb = -1.0, loadHiDb = -1.0, loadResQ = 1.4; // interação com o falante (-1 = automático pela NFB)
    EqBand preEq[2];                    // antes do 1º estágio
    EqBand postEq[5];                   // depois do power amp (carga do falante, transformador)
    double lvlG2 = 0.0, lvlG5 = 0.0, lvlG8 = 0.0; // nível de saída (dB) medido no amp real com gain em 2 / 5 / 8
};

// Lista todos os valores numéricos do circuito por nome (para ajuste fino e calibração)
template <class F> void forEachParam (CircuitSpec& c, F&& f)
{
    for (size_t i = 0; i < c.stages.size(); ++i)
    {
        auto& s = c.stages[i]; const std::string p = "s" + std::to_string (i) + ".";
        f (p + "rp", s.rp); f (p + "rk", s.rk); f (p + "ck", s.ck); f (p + "bPlus", s.bPlus);
        f (p + "plateCap", s.plateCap); f (p + "millerHz", s.millerHz);
    }
    for (size_t i = 0; i < c.nets.size(); ++i)
    {
        auto& n = c.nets[i]; const std::string p = "n" + std::to_string (i) + ".";
        f (p + "cc", n.cc); f (p + "rs", n.rs); f (p + "cs", n.cs); f (p + "rShunt", n.rShunt);
        f (p + "cBright", n.cBright); f (p + "extraGain", n.extraGain);
    }
    f ("tone.R1", c.tone.R1); f ("tone.R2", c.tone.R2); f ("tone.R3", c.tone.R3); f ("tone.R4", c.tone.R4);
    f ("tone.C1", c.tone.C1); f ("tone.C2", c.tone.C2); f ("tone.C3", c.tone.C3);
    f ("toneRecovery", c.toneRecovery);
    auto& w = c.power;
    f ("pow.rfb", w.rfb); f ("pow.rTail", w.rTail); f ("pow.loopGain", w.loopGain); f ("pow.presPot", w.presPot);
    f ("pow.presCap", w.presCap); f ("pow.resPot", w.resPot); f ("pow.resCap", w.resCap);
    f ("pow.piHeadroom", w.piHeadroom); f ("pow.powerKnee", w.powerKnee); f ("pow.asym", w.asym);
    f ("pow.sag", w.sag); f ("pow.loadResHz", w.loadResHz);
    f ("inputScale", c.inputScale); f ("outTrimDb", c.outTrimDb);
    f ("gainLo", c.gainLo); f ("gainHi", c.gainHi); f ("gainTaperDb", c.gainTaperDb); f ("masterTaperDb", c.masterTaperDb);
    f ("loadResDb", c.loadResDb); f ("loadHiDb", c.loadHiDb); f ("loadResQ", c.loadResQ);
    f ("trebleTaperDb", c.trebleTaperDb); f ("bassTaperDb", c.bassTaperDb); f ("midTaperDb", c.midTaperDb); f ("ssClip", c.ssClip); f ("cfPos", c.cfPos); f ("cfNeg", c.cfNeg);
    f ("lvlG2", c.lvlG2); f ("lvlG5", c.lvlG5); f ("lvlG8", c.lvlG8);
    for (int i = 0; i < 2; ++i)
    {
        const std::string p = "pre" + std::to_string (i) + ".";
        f (p + "type", c.preEq[i].type); f (p + "f", c.preEq[i].f); f (p + "q", c.preEq[i].q); f (p + "db", c.preEq[i].db);
    }
    for (int i = 0; i < 5; ++i)
    {
        const std::string p = "post" + std::to_string (i) + ".";
        f (p + "type", c.postEq[i].type); f (p + "f", c.postEq[i].f); f (p + "q", c.postEq[i].q); f (p + "db", c.postEq[i].db);
    }
}

// Ajustes externos (usado só pela ferramenta de calibração; vazio no plugin)
inline std::map<std::string, double>& specOverrides() { static std::map<std::string, double> m; return m; }
inline void applyOverrides (CircuitSpec& c, const std::string& prefix)
{
    auto& m = specOverrides();
    if (m.empty()) return;
    forEachParam (c, [&] (const std::string& name, double& v)
    {
        auto it = m.find (prefix + name);
        if (it != m.end()) v = it->second;
    });
}

// Filtro de banda (protótipo analógico -> bilinear)
inline void setBand (IIR2& f, const EqBand& b, double fs)
{
    const int type = (int) std::lround (b.type);
    const double w0 = 2.0 * juce::MathConstants<double>::pi * juce::jlimit (5.0, 0.45 * fs, b.f);
    const double q = juce::jmax (0.1, b.q), A = std::pow (10.0, b.db / 40.0), w2 = 1.0 / (w0 * w0);
    switch (type)
    {
        case 1: f.set (1.0, A / (q * w0), w2, 1.0, 1.0 / (A * q * w0), w2, fs); break;                       // pico
        default: f.set (1, 0, 0, 1, 0, 0, fs); break;
    }
    if (type == 2)       // shelf grave (RBJ): H(s) = A (s² + (√A/Q) s + A) / (A s² + (√A/Q) s + 1), s em unidades de w0
    {
        const double sa = std::sqrt (A);
        f.set (A * A, A * sa / (q * w0), A * w2, 1.0, sa / (q * w0), A * w2, fs);
    }
    else if (type == 3)  // shelf agudo: H(s) = A * (A s² + (√A/Q) s + 1) / (s² + (√A/Q) s + A)
    {
        const double sa = std::sqrt (A);
        f.set (A, A * sa / (q * w0), A * A * w2, A, sa / (q * w0), w2, fs);
    }
    else if (type == 4) f.set (1.0, 0.0, 0.0, 1.0, 1.0 / (q * w0), w2, fs);   // passa-baixa
    else if (type == 5) f.set (0.0, 0.0, w2, 1.0, 1.0 / (q * w0), w2, fs);     // passa-alta
}

// ---------- Amp completo ----------
class CircuitAmp
{
public:
    void prepare (double osRate, double baseRate)
    {
        osFs = osRate; fs = baseRate;
        configured = false;
    }

    void configure (const CircuitSpec& s)
    {
        spec = s;
        tone = spec.tone;   // valores do tone stack deste circuito
        tone.reset();
        const size_t n = spec.stages.size();
        tables.clear(); preShelf.assign (n, {}); plateLp.assign (n, {}); miller.assign (n, {});
        nets.assign (n, {}); netExtra.assign (n, 1.0f);
        for (size_t i = 0; i < n; ++i)
        {
            const auto& st = spec.stages[i];
            tables.push_back (getTable (st.bPlus, st.rp, st.rk, st.ck > 0.0));

            // Bypass do catodo: o estágio tem ganho total acima de fz e ganho reduzido abaixo.
            if (st.ck > 0.0)
            {
                const double mu = 100.0, ra = 62.5e3;
                const double gByp = mu * st.rp / (st.rp + ra);
                const double gUnb = mu * st.rp / (st.rp + ra + (mu + 1.0) * st.rk);
                const double wz = 1.0 / (st.rk * st.ck);
                const double wp = wz * gByp / gUnb;
                // H(s) = (gUnb/gByp) * (1 + s/wz) / (1 + s/wp)  -> 1 em alta frequência
                preShelf[i].set (gUnb / gByp, gUnb / gByp / wz, 1.0, 1.0 / wp, osFs);
            }
            else preShelf[i].set (1, 0, 1, 0, osFs);

            if (st.plateCap > 0.0) plateLp[i].set (1, 0, 1, st.rp * st.plateCap, osFs);
            else plateLp[i].set (1, 0, 1, 0, osFs);
            if (st.millerHz > 0.0) miller[i].set (1, 0, 1, 1.0 / (2.0 * juce::MathConstants<double>::pi * st.millerHz), osFs);
            else miller[i].set (1, 0, 1, 0, osFs);
        }
        configured = true;
        dirtyKnobs = true;
    }

    void setKnobs (double gain, double bass, double mid, double treble, double presence, double master, double resonance)
    {
        k = { gain, bass, mid, treble, presence, master, resonance };
        dirtyKnobs = true;
    }

    void reset()
    {
        for (auto& f : preShelf) f.reset();
        for (auto& f : plateLp) f.reset();
        for (auto& f : miller) f.reset();
        for (auto& f : nets) { f.hp.reset(); f.div.reset(); }
        tone.reset(); presEq.reset(); resEq.reset(); loadRes.reset(); loadHi.reset(); dcOut.reset();
        for (auto& f : preF) f.reset();
        for (auto& f : postF) f.reset();
        sagEnv = 0.0f;
    }

    // Processa no domínio sobreamostrado. Entrada em unidades digitais; saída normalizada.
    void processOS (float* x, int n)
    {
        if (! configured) return;
        if (dirtyKnobs) updateKnobs();
        const size_t ns = spec.stages.size();
        const float inScale = (float) spec.inputScale;
        const float piH = (float) spec.power.piHeadroom;
        const float knee = (float) spec.power.powerKnee;
        const float asym = (float) spec.power.asym;
        const float sagAmt = (float) spec.power.sag;
        const float sagAtk = 1.0f - std::exp (-1.0f / (0.02f * (float) osFs));
        const float sagRel = 1.0f - std::exp (-1.0f / (0.25f * (float) osFs));

        const float cfP = (float) spec.cfPos, cfN = (float) spec.cfNeg;
        const float ssc = (float) spec.ssClip;
        for (int i = 0; i < n; ++i)
        {
            float v = x[i] * inScale;
            v = preF[1].process (preF[0].process (v));
            if (ns == 0)
            {
                // Pré transistorizado: satura de leve (simétrico) e o knob Gain é o Volume
                if (ssc > 0.0f) v = ssc * std::tanh (v / ssc);
                v *= volumeGain;
            }
            for (size_t s = 0; s < ns; ++s)
            {
                v = miller[s].process (v);
                v = preShelf[s].process (v);
                v = tables[s]->lookup (v);
                v = plateLp[s].process (v);
                // rede seguinte (acoplamento + divisor/potenciômetro)
                auto& nt = nets[s];
                v = nt.hp.process (v);
                v = nt.div.process (v) * netExtra[s];
            }
            // Seguidor de catodo: quase ganho 1, satura de forma assimétrica com sinais muito grandes
            if (spec.cathodeFollower)
                v = v > 0.0f ? cfP * std::tanh (v / cfP) : cfN * std::tanh (v / cfN);

            v = tone.process (v) * (float) spec.toneRecovery;
            v *= masterGain;

            // Realimentação negativa: presence/resonance (pré-ênfase equivalente à malha fechada)
            v = resEq.process (presEq.process (v));

            // Inversor de fase + válvulas de potência (push-pull), com "sag" da fonte
            const float headroom = 1.0f - sagAmt * sagEnv;
            float u = v / piH;
            u = std::tanh (u * 0.9f) / 0.9f;                                 // inversor de fase
            const float drive = (u + asym * u * u) / headroom;
            float y = drive / std::pow (1.0f + std::pow (std::abs (drive), 2.0f * knee), 0.5f / knee); // saturação suave/dura
            y *= headroom;
            const float level = std::abs (y);
            sagEnv += (level - sagEnv) * (level > sagEnv ? sagAtk : sagRel);

            // Interação com a carga do alto-falante (mais forte quanto menor a realimentação)
            y = loadHi.process (loadRes.process (y));
            y = dcOut.process (y);
            for (auto& f : postF) y = f.process (y);
            x[i] = y * outTrim;
        }
    }

private:
    struct Net { IIR1 hp; IIR2 div; };

    void updateKnobs()
    {
        dirtyKnobs = false;
        auto taper = [] (double x, double db)
        {
            x = juce::jlimit (0.0, 1.0, x);
            if (db < 1.0) return x;
            const double r = std::pow (10.0, db / 20.0);
            return (std::pow (r, x) - 1.0) / (r - 1.0);
        };
        const double gainPos = taper (spec.gainLo + (spec.gainHi - spec.gainLo) * k.gain / 10.0, spec.gainTaperDb);
        volumeGain = (float) gainPos;
        setBand (preF[0], spec.preEq[0], osFs); setBand (preF[1], spec.preEq[1], osFs);
        for (int b = 0; b < 5; ++b) setBand (postF[(size_t) b], spec.postEq[b], osFs);
        for (size_t s = 0; s < spec.stages.size(); ++s)
        {
            const auto& is = s < spec.nets.size() ? spec.nets[s] : Interstage {};
            auto& nt = nets[s];
            const double rLoad = is.isGainPot ? is.rShunt : (is.rs > 0.0 ? is.rs + is.rShunt : is.rShunt);
            // passa-alta do capacitor de acoplamento com a carga seguinte
            nt.hp.set (0, is.cc * rLoad, 1, is.cc * rLoad, osFs);
            netExtra[s] = (float) is.extraGain;

            if (is.isGainPot)
            {
                // Série Rs||Cs -> topo do pot (Rt = (1-a)R || Cbright) -> cursor -> Rb = aR -> terra
                const double R = is.rShunt;
                const double Rb = juce::jmax (10.0, gainPos * R), Rt = juce::jmax (10.0, (1.0 - gainPos) * R);
                const double Rs = juce::jmax (1.0, is.rs);
                const double a = Rs * is.cs, b = Rt * is.cBright;
                nt.div.set (Rb, Rb * (a + b), Rb * a * b,
                            Rs + Rt + Rb, Rs * b + Rt * a + Rb * (a + b), Rb * a * b, osFs);
            }
            else if (is.rs > 0.0)
            {
                // divisor: Rs||Cs em série com Rshunt
                const double a = is.rs * is.cs, Rs = is.rs, Rsh = is.rShunt;
                // H = Rsh (1 + a s) / (Rs + Rsh (1 + a s))
                nt.div.set (Rsh, Rsh * a, 0, Rs + Rsh, Rsh * a, 0, osFs);
            }
            else nt.div.set (1, 0, 0, 1, 0, 0, osFs);
        }

        tone.set (taper (k.treble / 10.0, spec.trebleTaperDb), taper (k.mid / 10.0, spec.midTaperDb),
                  taper (k.bass / 10.0, spec.bassTaperDb), osFs);

        masterGain = (float) taper (k.master / 10.0, spec.masterTaperDb);

        // ---- Presence: rede no "tail" da realimentação (menos NFB em agudos = mais agudos)
        const auto& p = spec.power;
        const double A = p.loopGain > 0.0 ? p.loopGain * (p.rfb + p.rTail) / p.rTail : 0.0;  // ganho em malha aberta
        if (A > 0.0)
        {
            const double pr = 1.0 - k.presence / 10.0;
            const double Rx = p.presPot * pr * pr + 30.0;      // potenciômetro com curva
            const double C = p.presCap, Rf = p.rfb, Rt = p.rTail;
            const double d0 = Rf + Rt, d1 = C * (Rf * (Rt + Rx) + Rt * Rx);
            const double n0 = Rf + Rt + A * Rt, n1 = C * (Rf * (Rt + Rx) + Rt * Rx + A * Rt * Rx);
            const double K = 1.0 + A * Rt / (Rf + Rt);
            presEq.set (K * d0, K * d1, n0, n1, osFs);

            if (p.resPot > 0.0)
            {
                const double rr = k.resonance / 10.0;
                const double Rr = juce::jmax (50.0, p.resPot * rr * rr), Cr = p.resCap;
                const double K2 = 1.0 + A * Rt / (Rf + Rt);
                resEq.set (K2 * (Rf + Rt + Rr), K2 * (Rf + Rt) * Rr * Cr, (Rf + Rt + Rr + A * Rt), (Rf + Rt + A * Rt) * Rr * Cr, osFs);
            }
            else resEq.set (1, 0, 1, 0, osFs);
        }
        else
        {
            // Sem realimentação (ex.: Rec Modern): presence vira um shelf passivo mais discreto
            const double pres = (k.presence - 5.0) / 5.0;
            const double w = 2.0 * juce::MathConstants<double>::pi * 2500.0;
            const double g = std::pow (10.0, 5.0 * pres / 20.0);
            presEq.set (1.0, g / w, 1.0, 1.0 / w, osFs);
            resEq.set (1, 0, 1, 0, osFs);
        }

        // Carga reativa: pico na ressonância do falante e subida em agudos, atenuados pela NFB
        const double damping = 1.0 + (p.loopGain > 0.0 ? p.loopGain : 0.0);
        const double resDb = spec.loadResDb >= 0.0 ? spec.loadResDb : 9.0 / damping;
        const double hiDb = spec.loadHiDb >= 0.0 ? spec.loadHiDb : 7.0 / damping;
        auto peak = [this] (IIR2& f, double hz, double q, double db)
        {
            const double w0 = 2.0 * juce::MathConstants<double>::pi * hz, Ag = std::pow (10.0, db / 40.0);
            f.set (1.0, Ag / (q * w0), 1.0 / (w0 * w0), 1.0, 1.0 / (Ag * q * w0), 1.0 / (w0 * w0), osFs);
        };
        peak (loadRes, p.loadResHz, spec.loadResQ, resDb);
        const double wh = 2.0 * juce::MathConstants<double>::pi * 4000.0, gh = std::pow (10.0, hiDb / 20.0);
        loadHi.set (1.0, gh / wh, 1.0, 1.0 / wh, osFs);
        dcOut.set (0, 1.0 / (2.0 * juce::MathConstants<double>::pi * 15.0), 1.0, 1.0 / (2.0 * juce::MathConstants<double>::pi * 15.0), osFs);
        // Nível de saída em função do gain (interpolado entre as medições com gain 2, 5 e 8)
        const double g = juce::jlimit (2.0, 8.0, k.gain);
        const double lvl = g <= 5.0 ? spec.lvlG2 + (spec.lvlG5 - spec.lvlG2) * (g - 2.0) / 3.0
                                    : spec.lvlG5 + (spec.lvlG8 - spec.lvlG5) * (g - 5.0) / 3.0;
        outTrim = (float) std::pow (10.0, (spec.outTrimDb + lvl) / 20.0);
    }

    struct Knobs { double gain = 5, bass = 5, mid = 5, treble = 5, presence = 5, master = 5, resonance = 5; } k;
    CircuitSpec spec;
    double osFs = 192000, fs = 48000;
    bool configured = false, dirtyKnobs = true;
    std::vector<std::shared_ptr<StageTable>> tables;
    std::vector<IIR1> preShelf, plateLp, miller;
    std::vector<Net> nets;
    std::vector<float> netExtra;
    ToneStack tone;
    IIR1 presEq, resEq, loadHi, dcOut;
    IIR2 loadRes;
    std::array<IIR2, 2> preF;
    std::array<IIR2, 5> postF;
    float masterGain = 0.1f, outTrim = 1.0f, sagEnv = 0.0f, volumeGain = 1.0f;
};

// =====================================================================================
// Circuitos
// =====================================================================================

// Clean 120 (JC-120): pré transistorizado limpo, Volume antes do tone stack, power amp transistorizado
// (sem realimentação de válvula, sem presence). Valores do tone stack ajustados pelas medições do amp real.
inline CircuitSpec jcClean()
{
    CircuitSpec c;
    c.cathodeFollower = false;
    c.tone.R1 = 100e3; c.tone.R2 = 250e3; c.tone.R3 = 10e3; c.tone.R4 = 47e3;
    c.tone.C1 = 470e-12; c.tone.C2 = 47e-9; c.tone.C3 = 22e-9;
    c.trebleTaperDb = 0.0; c.bassTaperDb = 40.0;
    c.power = { 100e3, 4.7e3, 0.0, 25e3, 22e-9, 0.0, 0.0, 6.0, 3.0, 0.0, 0.0, 60.0 };
    c.ssClip = 8.0;
    c.inputScale = 4.0;
    c.loadResDb = 0.0; c.loadHiDb = 0.0;
    c.masterTaperDb = 0.0;
    // ---- Valores calibrados pelas medições do amp real (v0.9) ----
    c.tone.R1 = 76814.5; c.tone.R2 = 221270; c.tone.R3 = 4547.63;
    c.tone.R4 = 59433.9; c.tone.C1 = 3.30878e-10; c.tone.C2 = 3.86735e-08;
    c.tone.C3 = 1.05576e-07; c.trebleTaperDb = 0.994171; c.midTaperDb = 0.622785;
    c.bassTaperDb = 33.8341; c.preEq[0].type = 3; c.preEq[0].f = 935.85;
    c.preEq[0].q = 0.631045; c.preEq[0].db = 20.0287; c.postEq[0].type = 1;
    c.postEq[0].f = 77.3385; c.postEq[0].q = 0.698021; c.postEq[0].db = -1.5675;
    c.postEq[1].type = 1; c.postEq[1].f = 1548.31; c.postEq[1].q = 0.583782;
    c.postEq[1].db = -4.31684; c.postEq[2].type = 4; c.postEq[2].f = 10571.6;
    c.postEq[2].q = 0.693114; c.postEq[3].type = 5; c.postEq[3].f = 40.9074;
    c.postEq[3].q = 1.60703; c.inputScale = 1; c.ssClip = 2.26742;
    c.gainTaperDb = 0; c.masterTaperDb = 0; c.power.piHeadroom = 0.0857515;   // Volume linear: no 5 fica só 6 dB abaixo do 10 (medições foram com Volume 10)
    c.power.powerKnee = 1.91704; c.power.asym = 0.00663047; c.power.sag = 0;
    c.outTrimDb = 0;
    return c;
}

// JCM800 2203 (JVM canal 3 / OD1 laranja ≈ JCM800 com um pouco mais de ganho)
inline CircuitSpec brit80s()
{
    CircuitSpec c;
    // V1B: entrada High. Placa 100k, catodo 2.7k com 0.68uF
    c.stages.push_back ({ 100e3, 2.7e3, 0.68e-6, 300.0, 0.0, 18000.0 });
    // 22nF -> 470k||470pF -> pot de ganho 1M (bright cap 470pF)
    c.nets.push_back ({ 22e-9, 470e3, 470e-12, 1e6, true, 470e-12, 1.0 });
    // V1A: "cold clipper", catodo 10k sem bypass
    c.stages.push_back ({ 100e3, 10e3, 0.0, 300.0, 0.0, 0.0 });
    // 22nF -> 470k||470pF -> 470k para terra
    c.nets.push_back ({ 22e-9, 470e3, 470e-12, 470e3, false, 0.0, 1.0 });
    // V2A: catodo 820R com 0.68uF (no JVM laranja, um estágio mais quente)
    c.stages.push_back ({ 100e3, 820.0, 0.68e-6, 300.0, 0.0, 0.0 });
    c.nets.push_back ({ 1e-6, 0.0, 0.0, 1e6, false, 0.0, 1.0 });  // acoplamento direto ao seguidor de catodo
    c.cathodeFollower = true;
    // Tone stack Marshall: 220k treble, 1M bass, 25k mid, 33k slope, 470pF / 22nF / 22nF
    c.tone.R1 = 220e3; c.tone.R2 = 1e6; c.tone.R3 = 25e3; c.tone.R4 = 33e3;
    c.tone.C1 = 470e-12; c.tone.C2 = 22e-9; c.tone.C3 = 22e-9;
    c.trebleTaperDb = 40.0; c.bassTaperDb = 40.0;
    // Power: 4x EL34, NFB 100k / tail 4.7k, presence 22k + 0.1uF; resonance do JVM
    c.power = { 100e3, 4.7e3, 3.5, 25e3, 22e-9, 250e3, 6.8e-9, 1.6, 1.3, 0.07, 0.18, 95.0 };
    c.inputScale = 1.0;
    // ---- Valores calibrados pelas medições do amp real (v0.9) ----
    c.postEq[0].type = 1; c.postEq[0].q = 1; c.loadHiDb = 0; c.inputScale = 0.103009;
    c.gainTaperDb = 61.5848; c.gainLo = 0.27209; c.gainHi = 0.882916;
    c.nets[0].cBright = 6.65915e-10; c.nets[0].cs = 1.99798e-09; c.stages[0].ck = 1.72576e-06;
    c.stages[2].ck = 2.49796e-06; c.trebleTaperDb = 0.2; c.bassTaperDb = 39.3888;
    c.tone.C3 = 2.05479e-08; c.masterTaperDb = 37.346; c.power.piHeadroom = 1.91752;
    c.power.powerKnee = 0.891171; c.power.asym = 0.105812; c.power.loopGain = 0.767423;
    c.power.presCap = 2.466e-07; c.power.presPot = 18442.8; c.power.loadResHz = 114.735;
    c.loadResDb = 7.61352; c.loadResQ = 3.49528; c.power.sag = 0.426548;
    c.postEq[0].f = 1124.51; c.postEq[0].db = -0.904961; c.stages[1].rk = 5285.2;
    c.stages[2].rk = 1322.69; c.cfPos = 18.6265; c.cfNeg = 36.6263;
    return c;
}

// 5150 canal Lead (vermelho): estágios em cascata derivados do Soldano, EQ depois do pré,
// driver do tone stack, post gain, 4x 6L6 com Resonance e Presence na realimentação
inline CircuitSpec block5150()
{
    CircuitSpec c;
    c.stages.push_back ({ 100e3, 1.5e3, 1.0e-6, 330.0, 0.0, 18000.0 });     // V1: catodo 1.5k com 1uF (NP)
    c.nets.push_back ({ 22e-9, 470e3, 470e-12, 1e6, true, 0.0, 1.0 });      // Pre Gain
    c.stages.push_back ({ 100e3, 39e3, 0.0, 330.0, 0.0, 0.0 });             // estágio frio (corte assimétrico)
    c.nets.push_back ({ 2.2e-9, 470e3, 0.0, 1e6, false, 0.0, 1.0 });        // cap pequeno: graves apertados
    c.stages.push_back ({ 100e3, 1.5e3, 0.68e-6, 330.0, 0.0, 0.0 });
    c.nets.push_back ({ 22e-9, 470e3, 0.0, 1e6, false, 0.0, 1.0 });
    c.stages.push_back ({ 100e3, 1.5e3, 0.68e-6, 330.0, 1e-9, 0.0 });
    c.nets.push_back ({ 1e-6, 0.0, 0.0, 1e6, false, 0.0, 1.0 });
    c.cathodeFollower = true;
    c.tone.R1 = 250e3; c.tone.R2 = 1e6; c.tone.R3 = 25e3; c.tone.R4 = 47e3;
    c.tone.C1 = 250e-12; c.tone.C2 = 22e-9; c.tone.C3 = 22e-9;
    c.trebleTaperDb = 40.0; c.bassTaperDb = 40.0;
    c.toneRecovery = 1.0;
    c.power = { 82e3, 4.7e3, 3.0, 25e3, 22e-9, 250e3, 6.8e-9, 1.7, 1.15, 0.05, 0.12, 100.0 };
    // ---- Valores calibrados pelas medições do amp real (v0.9) ----
    c.postEq[0].type = 1; c.postEq[0].q = 0.8; c.postEq[1].type = 4;
    c.postEq[1].q = 0.7; c.loadHiDb = 0; c.inputScale = 0.655268;
    c.gainTaperDb = 4.79529; c.gainLo = 0.000610282; c.gainHi = 0.99006;
    c.stages[0].ck = 7.83774e-06; c.nets[1].cc = 2.38391e-09; c.stages[2].ck = 5.41785e-07;
    c.stages[3].plateCap = 1.54877e-10; c.trebleTaperDb = 4.82463; c.bassTaperDb = 39.9714;
    c.midTaperDb = 0.868509; c.tone.C1 = 2.4086e-10; c.tone.C3 = 2.37894e-08;
    c.tone.R4 = 73982.8; c.cfPos = 27.1753; c.power.piHeadroom = 19.9998;
    c.power.powerKnee = 0.741028; c.power.asym = 0.495127; c.power.loopGain = 7.24697;
    c.power.presCap = 8.47608e-08; c.power.presPot = 155952; c.power.resCap = 3.94052e-09;
    c.power.resPot = 374421; c.power.loadResHz = 99.486; c.loadResDb = 2.20924;
    c.loadResQ = 0.996096; c.power.sag = 0.0695236; c.postEq[0].f = 383.054;
    c.postEq[0].db = -2.60564; c.postEq[1].f = 16215.8;
    return c;
}

// Dual Rectifier, canal de alto ganho (um circuito só, calibrado pelas medições do amp real)
inline CircuitSpec recRectifier()
{
    CircuitSpec c;
    c.stages.push_back ({ 100e3, 1.5e3, 1.0e-6, 290.0, 0.0, 18000.0 });
    c.nets.push_back ({ 22e-9, 470e3, 250e-12, 1e6, true, 180e-12, 1.0 });    // Gain
    c.stages.push_back ({ 100e3, 1.8e3, 1.0e-6, 290.0, 0.0, 0.0 });
    c.nets.push_back ({ 22e-9, 475e3, 0.0, 1e6, false, 0.0, 1.0 });
    c.stages.push_back ({ 100e3, 39e3, 0.0, 290.0, 0.0, 0.0 });                 // estágio frio
    c.nets.push_back ({ 22e-9, 475e3, 680e-12, 1e6, false, 0.0, 1.0 });
    c.stages.push_back ({ 100e3, 1.5e3, 0.68e-6, 290.0, 1e-9, 0.0 });
    c.nets.push_back ({ 1e-6, 0.0, 0.0, 1e6, false, 0.0, 1.0 });
    c.cathodeFollower = true;
    // Tone stack (base Marshall; capacitores e R4 ajustados pela medição)
    c.tone.R1 = 220e3; c.tone.R2 = 1e6; c.tone.R3 = 25e3; c.tone.R4 = 33e3;
    c.tone.C1 = 470e-12; c.tone.C2 = 22e-9; c.tone.C3 = 22e-9;
    c.trebleTaperDb = 40.0; c.bassTaperDb = 40.0;
    c.power = { 100e3, 4.7e3, 0.0, 25e3, 22e-9, 0.0, 0.0, 1.5, 1.6, 0.04, 0.08, 90.0 };
    // ---- Valores calibrados pelas medições do amp real (v0.9) ----
    c.postEq[0].type = 1; c.postEq[1].type = 4; c.postEq[1].q = 0.7;
    c.loadHiDb = 0; c.inputScale = 1.45584; c.gainTaperDb = 23.3349;
    c.gainLo = 0.094371; c.gainHi = 0.894185; c.stages[0].ck = 4.46389e-07;
    c.stages[1].ck = 2.98418e-06; c.nets[0].cBright = 7.38083e-10; c.nets[2].cs = 5.41807e-10;
    c.stages[3].plateCap = 7.20247e-10; c.trebleTaperDb = 27.7764; c.bassTaperDb = 18.7687;
    c.midTaperDb = 11.8644; c.tone.C1 = 8.60874e-10; c.tone.C2 = 5.72471e-08;
    c.tone.C3 = 2.00683e-08; c.tone.R4 = 100869; c.cfPos = 50.1179;
    c.power.piHeadroom = 0.650542; c.power.powerKnee = 0.500013; c.power.asym = 0.220913;
    c.power.loopGain = 4.44378; c.power.presCap = 3.66085e-08; c.power.presPot = 21976.9;
    c.power.loadResHz = 99.4068; c.loadResDb = 6.35002; c.loadResQ = 1.67886;
    c.power.sag = 0.498322; c.postEq[0].f = 672.991; c.postEq[0].q = 0.689155;
    c.postEq[0].db = -18.0241; c.postEq[1].f = 8503.76;
    c.lvlG2 = 3.3; c.lvlG5 = 3.3; c.lvlG8 = 0.0;   // no Mesa real, gain 2 e 5 saem ~3 dB mais altos que gain 8
    return c;
}
} // namespace vlv::tube

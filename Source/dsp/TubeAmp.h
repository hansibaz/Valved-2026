#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <memory>
#include <map>
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

struct CircuitSpec
{
    std::vector<StageSpec> stages;
    std::vector<Interstage> nets;       // nets[i] vem DEPOIS de stages[i]
    bool cathodeFollower = true;
    ToneStack tone;                     // valores dos componentes
    bool trebleLog = false, bassLog = true;
    double toneRecovery = 1.0;          // ganho do estágio após o tone stack (5150)
    double masterPot = 1e6;
    PowerSpec power;
    double inputScale = 1.0;            // volts por unidade digital na entrada
    double outTrimDb = 0.0;
};

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

        for (int i = 0; i < n; ++i)
        {
            float v = x[i] * inScale;
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
                v = v > 0.0f ? 45.0f * std::tanh (v / 45.0f) : 90.0f * std::tanh (v / 90.0f);

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
            x[i] = dcOut.process (y) * outTrim;
        }
    }

private:
    struct Net { IIR1 hp; IIR2 div; };

    void updateKnobs()
    {
        dirtyKnobs = false;
        const double gainPos = audioTaper (k.gain / 10.0);
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

        const double t = spec.trebleLog ? audioTaper (k.treble / 10.0) : k.treble / 10.0;
        const double l = spec.bassLog ? audioTaper (k.bass / 10.0) : k.bass / 10.0;
        tone.set (t, k.mid / 10.0, l, osFs);

        masterGain = (float) audioTaper (k.master / 10.0);

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
        const double resDb = 9.0 / damping, hiDb = 7.0 / damping;
        auto peak = [this] (IIR2& f, double hz, double q, double db)
        {
            const double w0 = 2.0 * juce::MathConstants<double>::pi * hz, Ag = std::pow (10.0, db / 40.0);
            f.set (1.0, Ag / (q * w0), 1.0 / (w0 * w0), 1.0, 1.0 / (Ag * q * w0), 1.0 / (w0 * w0), osFs);
        };
        peak (loadRes, p.loadResHz, 1.4, resDb);
        const double wh = 2.0 * juce::MathConstants<double>::pi * 4000.0, gh = std::pow (10.0, hiDb / 20.0);
        loadHi.set (1.0, gh / wh, 1.0, 1.0 / wh, osFs);
        dcOut.set (0, 1.0 / (2.0 * juce::MathConstants<double>::pi * 15.0), 1.0, 1.0 / (2.0 * juce::MathConstants<double>::pi * 15.0), osFs);
        outTrim = (float) std::pow (10.0, spec.outTrimDb / 20.0);
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
    float masterGain = 0.1f, outTrim = 1.0f, sagEnv = 0.0f;
};

// =====================================================================================
// Circuitos
// =====================================================================================

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
    c.trebleLog = true; c.bassLog = true;
    // Power: 4x EL34, NFB 100k / tail 4.7k, presence 22k + 0.1uF; resonance do JVM
    c.power = { 100e3, 4.7e3, 3.5, 25e3, 22e-9, 250e3, 6.8e-9, 1.6, 1.3, 0.07, 0.18, 95.0 };
    c.inputScale = 1.0;
    return c;
}

// JVM canal 4 (OD2) vermelho: topologia JCM800 + estágios extras em cascata, médios centrados ~500 Hz
inline CircuitSpec britModern()
{
    CircuitSpec c;
    c.stages.push_back ({ 100e3, 2.7e3, 0.68e-6, 300.0, 0.0, 18000.0 });
    c.nets.push_back ({ 22e-9, 470e3, 470e-12, 1e6, true, 470e-12, 1.0 });
    c.stages.push_back ({ 100e3, 1.5e3, 0.68e-6, 300.0, 0.0, 0.0 });        // estágio extra (modo vermelho)
    c.nets.push_back ({ 22e-9, 470e3, 330e-12, 470e3, false, 0.0, 1.0 });
    c.stages.push_back ({ 100e3, 10e3, 0.0, 300.0, 0.0, 0.0 });             // cold clipper
    c.nets.push_back ({ 22e-9, 470e3, 470e-12, 470e3, false, 0.0, 1.0 });
    c.stages.push_back ({ 220e3, 1.5e3, 1.0e-6, 300.0, 470e-12, 0.0 });     // último triodo: placa 220k + cap de placa
    c.nets.push_back ({ 1e-6, 0.0, 0.0, 1e6, false, 0.0, 1.0 });
    c.cathodeFollower = true;
    c.tone.R1 = 220e3; c.tone.R2 = 1e6; c.tone.R3 = 25e3; c.tone.R4 = 33e3;
    c.tone.C1 = 470e-12; c.tone.C2 = 22e-9; c.tone.C3 = 33e-9;              // cap de médios maior -> centro ~500 Hz
    c.trebleLog = true; c.bassLog = true;
    c.power = { 100e3, 4.7e3, 3.5, 25e3, 22e-9, 250e3, 6.8e-9, 1.6, 1.3, 0.07, 0.18, 95.0 };
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
    c.trebleLog = true; c.bassLog = true;
    c.toneRecovery = 1.0;
    c.power = { 82e3, 4.7e3, 3.0, 25e3, 22e-9, 250e3, 6.8e-9, 1.7, 1.15, 0.05, 0.12, 100.0 };
    return c;
}

// Dual Rectifier canal de alto ganho. Modern: sem realimentação negativa, mais ganho.
// Vintage: com realimentação (presence age nela), um estágio com menos ganho em graves, mais médios.
inline CircuitSpec recRectifier (bool modern)
{
    CircuitSpec c;
    c.stages.push_back ({ 100e3, 1.5e3, 1.0e-6, 290.0, 0.0, 18000.0 });
    c.nets.push_back ({ 22e-9, 470e3, 250e-12, 1e6, true, 180e-12, 1.0 });    // Gain
    c.stages.push_back ({ 100e3, 1.8e3, modern ? 1.0e-6 : 0.1e-6, 290.0, 0.0, 0.0 });
    c.nets.push_back ({ 22e-9, 475e3, 0.0, 1e6, false, 0.0, 1.0 });
    c.stages.push_back ({ 100e3, 39e3, 0.0, 290.0, 0.0, 0.0 });                 // estágio frio
    c.nets.push_back ({ 22e-9, 475e3, 680e-12, 1e6, false, 0.0, 1.0 });
    c.stages.push_back ({ 100e3, 1.5e3, 0.68e-6, 290.0, 1e-9, 0.0 });
    c.nets.push_back ({ 1e-6, 0.0, 0.0, 1e6, false, 0.0, 1.0 });
    c.cathodeFollower = true;
    c.tone.R1 = 250e3; c.tone.R2 = 250e3; c.tone.R3 = 25e3; c.tone.R4 = 47e3;   // EQ no fim do pré
    c.tone.C1 = 500e-12; c.tone.C2 = 22e-9; c.tone.C3 = 22e-9;
    c.trebleLog = true; c.bassLog = true;
    if (modern) c.power = { 100e3, 4.7e3, 0.0, 25e3, 22e-9, 0.0, 0.0, 1.5, 1.6, 0.04, 0.08, 90.0 };
    else        c.power = { 100e3, 4.7e3, 3.0, 25e3, 22e-9, 0.0, 0.0, 1.5, 1.2, 0.05, 0.10, 90.0 };
    return c;
}
} // namespace vlv::tube

#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <array>
#include <cmath>
#include "Biquad.h"
#include "TubeAmp.h"

// =====================================================================================
// Pedais modelados a partir dos circuitos.
//  - Partes lineares (filtros, tone stacks, somadores) são resolvidas por análise nodal
//    (MNA) com o modelo trapezoidal dos capacitores: é a resposta exata da rede de
//    resistores, capacitores e amp-ops ideais, amostra por amostra.
//  - Diodos são resolvidos pela equação de Shockley (Newton-Raphson a cada amostra),
//    tanto em realimentação (TS, SD-1, King of Tone) quanto para a terra (Klon, KoT "distortion").
//  - Amp-ops saturam nos trilhos da fonte (9 V ou 18 V conforme o pedal).
// =====================================================================================
namespace vlv::pedal
{
constexpr double Vt = 0.02585;

struct Diode { double is, n; };
constexpr Diode D1N914   { 2.52e-9, 1.752 };   // 1N914 / 1N4148
constexpr Diode D1S2473  { 4.0e-10, 1.90 };    // 1S2473 (SD-1), Vf ~0,8-0,9 V
constexpr Diode D1N34A   { 2.0e-7,  1.30 };    // germânio 1N34A (Klon), Vf ~0,3 V

inline double audioTaper (double x) { x = juce::jlimit (0.0, 1.0, x); return (std::pow (10.0, 2.0 * x) - 1.0) / 99.0; }

// ------------------------------------------------------------------------------------
// Circuito linear genérico (MNA). Nó 0 = terra (referência AC / meia-fonte).
// ------------------------------------------------------------------------------------
class LinearCircuit
{
public:
    void clear() { res.clear(); caps.clear(); ops.clear(); nNodes = 1; }
    int node() { return nNodes++; }
    int addR (int a, int b, double r) { res.push_back ({ a, b, r }); return (int) res.size() - 1; }
    void addC (int a, int b, double c) { caps.push_back ({ a, b, c, 0.0, 0.0, 0.0 }); }
    void addOpAmp (int plus, int minus, int out) { ops.push_back ({ plus, minus, out }); }
    void setInput (int n) { inNode = n; }
    void setOutput (int n) { outNode = n; }
    void setR (int idx, double r) { if (res[(size_t) idx].r != r) { res[(size_t) idx].r = juce::jmax (1.0, r); dirty = true; } } // NOLINT

    void prepare (double sampleRate) { T = 1.0 / sampleRate; dirty = true; reset(); }
    void reset() { for (auto& c : caps) { c.v = 0; c.i = 0; } }

    inline float process (float vin) noexcept
    {
        if (dirty) build();
        // RHS
        std::fill (b.begin(), b.end(), 0.0);
        for (auto& c : caps)
        {
            const double J = c.g * c.v + c.i;        // fonte de corrente equivalente (histórico)
            if (c.a > 0) b[(size_t) (c.a - 1)] += J;
            if (c.b > 0) b[(size_t) (c.b - 1)] -= J;
        }
        b[(size_t) (nNodes - 1)] = vin;              // fonte de entrada
        // x = A^-1 b
        for (int r = 0; r < N; ++r)
        {
            double s = 0.0;
            const double* row = &inv[(size_t) (r * N)];
            for (int k = 0; k < N; ++k) s += row[k] * b[(size_t) k];
            x[(size_t) r] = s;
        }
        auto V = [this] (int n) { return n > 0 ? x[(size_t) (n - 1)] : 0.0; };
        for (auto& c : caps)
        {
            const double v = V (c.a) - V (c.b);
            c.i = c.g * v - (c.g * c.v + c.i);
            c.v = v;
        }
        return (float) V (outNode);
    }

    double nodeVoltage (int n) const { return n > 0 ? x[(size_t) (n - 1)] : 0.0; }

private:
    struct Res { int a, b; double r; };
    struct Cap { int a, b; double c, g, v, i; };
    struct Op { int plus, minus, out; };

    void build()
    {
        dirty = false;
        const int nv = nNodes - 1;
        N = nv + 1 + (int) ops.size();
        std::vector<double> A ((size_t) (N * N), 0.0);
        auto at = [&] (int r, int c) -> double& { return A[(size_t) (r * N + c)]; };
        auto stamp = [&] (int a, int bnode, double g)
        {
            if (a > 0) at (a - 1, a - 1) += g;
            if (bnode > 0) at (bnode - 1, bnode - 1) += g;
            if (a > 0 && bnode > 0) { at (a - 1, bnode - 1) -= g; at (bnode - 1, a - 1) -= g; }
        };
        for (auto& r : res) stamp (r.a, r.b, 1.0 / r.r);
        for (auto& c : caps) { c.g = 2.0 * c.c / T; stamp (c.a, c.b, c.g); }
        // Fonte de tensão de entrada (linha nv)
        at (inNode - 1, nv) += 1.0;
        at (nv, inNode - 1) += 1.0;
        // Amp-ops ideais
        for (size_t k = 0; k < ops.size(); ++k)
        {
            const int row = nv + 1 + (int) k;
            at (ops[k].out - 1, row) += 1.0;                 // corrente de saída entra no nó de saída
            if (ops[k].plus > 0) at (row, ops[k].plus - 1) += 1.0;
            if (ops[k].minus > 0) at (row, ops[k].minus - 1) -= 1.0;
        }
        // Inversão (Gauss-Jordan)
        inv.assign ((size_t) (N * N), 0.0);
        for (int i = 0; i < N; ++i) inv[(size_t) (i * N + i)] = 1.0;
        for (int col = 0; col < N; ++col)
        {
            int piv = col; double best = std::abs (at (col, col));
            for (int r = col + 1; r < N; ++r) if (std::abs (at (r, col)) > best) { best = std::abs (at (r, col)); piv = r; }
            if (piv != col)
                for (int k = 0; k < N; ++k) { std::swap (at (col, k), at (piv, k)); std::swap (inv[(size_t) (col * N + k)], inv[(size_t) (piv * N + k)]); }
            const double d = at (col, col);
            if (std::abs (d) < 1e-30) continue;
            for (int k = 0; k < N; ++k) { at (col, k) /= d; inv[(size_t) (col * N + k)] /= d; }
            for (int r = 0; r < N; ++r)
            {
                if (r == col) continue;
                const double f = at (r, col);
                if (f == 0.0) continue; // NOLINT
                for (int k = 0; k < N; ++k) { at (r, k) -= f * at (col, k); inv[(size_t) (r * N + k)] -= f * inv[(size_t) (col * N + k)]; }
            }
        }
        b.assign ((size_t) N, 0.0);
        x.assign ((size_t) N, 0.0);
        nNodes = nv + 1;
    }

    std::vector<Res> res;
    std::vector<Cap> caps;
    std::vector<Op> ops;
    std::vector<double> inv, b, x;
    int nNodes = 1, N = 0, inNode = 1, outNode = 1;
    double T = 1.0 / 192000.0;
    bool dirty = true;
};

// ------------------------------------------------------------------------------------
// Corrente de um ramo de diodos: nFwd diodos em série num sentido, nRev no outro,
// opcionalmente em série com um resistor. Tabela I(V) monotônica pré-calculada.
// ------------------------------------------------------------------------------------
class DiodeBranch
{
public:
    void build (Diode d, int nFwd, int nRev, double seriesR = 0.0)
    {
        table.resize (size);
        for (int i = 0; i < size; ++i)
        {
            const double v = lo + (hi - lo) * (double) i / (double) (size - 1);
            table[(size_t) i] = solve (v, d, nFwd, nRev, seriesR);
        }
    }
    inline double current (double v) const noexcept
    {
        const double p = (v - lo) * scale;
        if (p <= 0.0) return table.front() + (v - lo) * slopeEnd (true);
        if (p >= (double) (size - 1)) return table.back() + (v - hi) * slopeEnd (false);
        const int i = (int) p; const double t = p - (double) i;
        return table[(size_t) i] + (table[(size_t) i + 1] - table[(size_t) i]) * t;
    }
    inline double conductance (double v) const noexcept
    {
        const double p = juce::jlimit (0.0, (double) (size - 2), (v - lo) * scale);
        const int i = (int) p;
        return juce::jmax (1e-12, (table[(size_t) i + 1] - table[(size_t) i]) * scale);
    }

private:
    double slopeEnd (bool low) const
    {
        return low ? (table[1] - table[0]) * scale : (table[(size_t) size - 1] - table[(size_t) size - 2]) * scale;
    }
    static double solve (double v, Diode d, int nF, int nR, double rs)
    {
        // corrente i tal que v = i*rs + Vdiodos(i)
        auto vd = [&] (double i)
        {
            if (i >= 0.0) return nF > 0 ? nF * d.n * Vt * std::log1p (i / d.is) : 1e9 * i;
            return nR > 0 ? -nR * d.n * Vt * std::log1p (-i / d.is) : 1e9 * i;
        };
        double loI = -1.0, hiI = 1.0;
        for (int it = 0; it < 100; ++it)
        {
            const double m = 0.5 * (loI + hiI);
            if (m * rs + vd (m) > v) hiI = m; else loI = m;
        }
        return 0.5 * (loI + hiI);
    }
    static constexpr int size = 8192;
    static constexpr double lo = -12.0, hi = 12.0;
    const double scale = (size - 1) / (hi - lo);
    std::vector<double> table;
};

// ------------------------------------------------------------------------------------
// Estágio clipper com diodos na realimentação de um amp-op NÃO-inversor (TS808, TS9, SD-1).
//   Vout = Vin + Vf ; corrente do ramo de terra (Rg + Cg) = corrente da realimentação
//   (Rf || Cf || diodos). Resolve Vf por Newton a cada amostra.
// ------------------------------------------------------------------------------------
class FeedbackClipper
{
public:
    void prepare (double osRate) { T = 1.0 / osRate; reset(); }
    void reset() { vCg = iCg = vf = iCf = 0.0; }
    void set (double rg, double cg, double rf, double cf, double railV)
    {
        Rg = rg; Cg = cg; Rf = rf; Cf = cf; rail = railV;
        kG = T / (2.0 * Cg); gF = 2.0 * Cf / T;
    }
    DiodeBranch diodes;

    inline float process (float vinF) noexcept
    {
        const double vin = vinF;
        // ramo de terra (Rg em série com Cg), trapezoidal
        const double i = (vin - vCg - kG * iCg) / (Rg + kG);
        vCg += kG * (i + iCg); iCg = i;
        // Vf: i = Vf/Rf + gF*(Vf - vfPrev) - iCfPrev + Id(Vf)
        const double hist = gF * vf + iCf;
        double v = vf;
        for (int it = 0; it < 8; ++it)
        {
            const double f = v / Rf + gF * v - hist + diodes.current (v) - i;
            const double df = 1.0 / Rf + gF + diodes.conductance (v);
            const double step = f / df;
            v -= juce::jlimit (-0.5, 0.5, step);
            if (std::abs (step) < 1e-7) break;
        }
        iCf = gF * v - hist;
        vf = v;
        const double out = vin + v;
        return (float) (rail * std::tanh (out / rail));    // saturação suave do amp-op nos trilhos
    }

private:
    double T = 1.0 / 192000.0, Rg = 4.7e3, Cg = 47e-9, Rf = 51e3, Cf = 51e-12, rail = 3.5;
    double kG = 0.0, gF = 0.0, vCg = 0, iCg = 0, vf = 0, iCf = 0;
};

// Clipper INVERSOR com diodos (com resistor em série) na realimentação (King of Tone / Bluesbreaker)
class InvertingClipper
{
public:
    void prepare (double osRate) { T = 1.0 / osRate; reset(); }
    void reset() { vf = iCf = vCin = iCin = 0.0; }
    void set (double rin, double cin, double rf, double cf, double railV)
    {
        Rin = rin; Cin = cin; Rf = rf; Cf = cf; rail = railV;
        kIn = Cin > 0.0 ? T / (2.0 * Cin) : 0.0; gF = 2.0 * Cf / T;
    }
    bool useDiodes = true;
    DiodeBranch diodes;

    inline float process (float vinF) noexcept
    {
        const double vin = vinF;
        // corrente de entrada para o terra virtual (Rin em série com Cin)
        const double i = Cin > 0.0 ? (vin - vCin - kIn * iCin) / (Rin + kIn) : vin / Rin;
        if (Cin > 0.0) { vCin += kIn * (i + iCin); iCin = i; }
        const double hist = gF * vf + iCf;
        double v = vf;
        for (int it = 0; it < 8; ++it)
        {
            const double id = useDiodes ? diodes.current (v) : 0.0;
            const double f = v / Rf + gF * v - hist + id - i;
            const double df = 1.0 / Rf + gF + (useDiodes ? diodes.conductance (v) : 0.0);
            const double step = f / df;
            v -= juce::jlimit (-0.5, 0.5, step);
            if (std::abs (step) < 1e-7) break;
        }
        iCf = gF * v - hist;
        vf = v;
        return (float) (rail * std::tanh (-v / rail));
    }

private:
    double T = 1.0 / 192000.0, Rin = 10e3, Cin = 0.0, Rf = 220e3, Cf = 100e-12, rail = 3.5;
    double kIn = 0, gF = 0, vf = 0, iCf = 0, vCin = 0, iCin = 0;
};

// Diodos para a terra depois de um resistor (Klon 1N34A, KoT modo distortion)
class ShuntClipper
{
public:
    void set (double rSeries) { R = rSeries; }
    DiodeBranch diodes;
    inline float process (float vF) noexcept
    {
        const double v = vF;
        double vn = vPrev;
        for (int it = 0; it < 10; ++it)
        {
            const double f = (vn - v) / R + diodes.current (vn);
            const double df = 1.0 / R + diodes.conductance (vn);
            const double step = f / df;
            vn -= juce::jlimit (-0.5, 0.5, step);
            if (std::abs (step) < 1e-8) break;
        }
        vPrev = vn;
        return (float) vn;
    }
    void reset() { vPrev = 0.0; }
private:
    double R = 1e3, vPrev = 0.0;
};

// =====================================================================================
// Base: sobreamostragem 4x, bypass suave
// =====================================================================================
class PedalBase
{
public:
    virtual ~PedalBase() = default;
    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        os = std::make_unique<juce::dsp::Oversampling<float>> (1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        os->initProcessing ((size_t) juce::jmax (1, maxBlock));
        osFs = fs * 4.0;
        dry.assign ((size_t) juce::jmax (1, maxBlock), 0.0f);
        prepareCircuit();
        dirty = true;
    }
    void setEnabled (bool e) { enabled = e; }
    void process (float* data, int n)
    {
        if (! enabled && mix <= 0.0f) return;
        if (enabled && mix <= 0.0f) { resetCircuit(); if (os) os->reset(); }
        if (dirty) { dirty = false; updateCircuit(); }
        if ((int) dry.size() < n) dry.resize ((size_t) n);
        std::copy (data, data + n, dry.begin());
        float* ch[1] = { data };
        juce::dsp::AudioBlock<float> block (ch, 1, (size_t) n);
        auto up = os->processSamplesUp (block);
        processOS (up.getChannelPointer (0), (int) up.getNumSamples());
        os->processSamplesDown (block);
        const float target = enabled ? 1.0f : 0.0f, step = 1.0f / (0.01f * (float) fs);
        for (int i = 0; i < n; ++i)
        {
            if (mix < target) mix = juce::jmin (target, mix + step);
            else if (mix > target) mix = juce::jmax (target, mix - step);
            data[i] = dry[(size_t) i] + (data[i] * outGain - dry[(size_t) i]) * mix;
        }
    }

protected:
    virtual void prepareCircuit() = 0;
    virtual void resetCircuit() = 0;
    virtual void updateCircuit() = 0;
    virtual void processOS (float* x, int n) = 0;
    void markDirty() { dirty = true; }

    double fs = 48000, osFs = 192000;
    float outGain = 1.0f;
private:
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    std::vector<float> dry;
    bool enabled = false, dirty = true;
    float mix = 0.0f;
};

// =====================================================================================
// GREEN DRIVE — Ibanez TS808 / TS9
//  Buffer de entrada (seguidor de emissor) -> 1 uF / 10k -> amp-op não-inversor:
//  Rg = 4k7 + 47 nF (passa-alta 720 Hz), Rf = 51k + Drive 500k (log), Cf = 51 pF, 1N914 x2.
//  Tone: 1k / 220 nF (723 Hz) -> amp-op com pot 20k entre (+) e (-), cursor em 220R + 220 nF
//  (3,2 kHz), realimentação 1k. Volume 100k (log). Saída: TS808 100R / 10k; TS9 470R / 100k.
// =====================================================================================
class TubeScreamer : public PedalBase
{
public:
    void setParams (float drive, float tone, float level, bool ts9Mode)
    {
        if (drive != pDrive || tone != pTone || level != pLevel || ts9Mode != ts9) // NOLINT
        { pDrive = drive; pTone = tone; pLevel = level; ts9 = ts9Mode; markDirty(); }
    }
protected:
    void prepareCircuit() override
    {
        inHp.set (0, 0.02e-6 * 446e3, 1, 0.02e-6 * 446e3, osFs);        // C1 20 nF / ~446k
        cplHp.set (0, 1e-6 * 10e3, 1, 1e-6 * 10e3, osFs);               // C2 1 uF / R5 10k
        clip.prepare (osFs);
        clip.diodes.build (D1N914, 1, 1);
        // Tone stage (MNA)
        tone.clear();
        const int nIn = tone.node(), nX = tone.node(), nN = tone.node(), nW = tone.node(), nM = tone.node(), nO = tone.node();
        tone.setInput (nIn); tone.setOutput (nO);
        tone.addR (nIn, nX, 1e3);           // R7
        tone.addC (nX, 0, 0.22e-6);         // C5
        potA = tone.addR (nX, nW, 10e3);    // tone pot (lado +)
        potB = tone.addR (nW, nN, 10e3);    // tone pot (lado -)
        tone.addR (nW, nM, 220.0);          // R8
        tone.addC (nM, 0, 0.22e-6);         // C6
        tone.addR (nO, nN, 1e3);            // realimentação 1k
        tone.addOpAmp (nX, nN, nO);
        tone.prepare (osFs);
        outHp.set (0, 1e-6 * 100e3, 1, 1e-6 * 100e3, osFs);             // C 1 uF + volume 100k
    }
    void resetCircuit() override { inHp.reset(); cplHp.reset(); clip.reset(); tone.reset(); outHp.reset(); }
    void updateCircuit() override
    {
        const double rf = 51e3 + 500e3 * audioTaper (pDrive / 10.0);
        clip.set (4.7e3, 47e-9, rf, 51e-12, 3.4);
        const double t = juce::jlimit (0.002, 0.998, pTone / 10.0);
        tone.setR (potA, 20e3 * t);
        tone.setR (potB, 20e3 * (1.0 - t));
        // Saída: divisor do resistor série com a carga (entrada de ~1M do amp)
        const double rs = ts9 ? 470.0 : 100.0, rl = ts9 ? 100e3 : 10e3;
        const double load = 1e6 * rl / (1e6 + rl);
        outGain = (float) (audioTaper (pLevel / 10.0) * load / (load + rs));
    }
    void processOS (float* x, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            float v = inHp.process (x[i]);
            v = cplHp.process (v);
            v = clip.process (v);
            v = tone.process (v);
            v = 3.4f * std::tanh (v / 3.4f);
            x[i] = outHp.process (v);
        }
    }
private:
    vlv::tube::IIR1 inHp, cplHp, outHp;
    FeedbackClipper clip;
    LinearCircuit tone;
    int potA = 0, potB = 0;
    float pDrive = 5, pTone = 5, pLevel = 5;
    bool ts9 = false;
};

// =====================================================================================
// YELLOW DRIVE — Boss SD-1
//  Mesma família do TS, com clipping ASSIMÉTRICO: dois 1S2473 num sentido, um no outro.
//  Rg = 4k7 + 47 nF, Rf = 33k + Drive 1M (log), Cf = 47 pF. Tone com os valores do SD-1
//  (centrado mais alto, ~1 kHz, agudo de cima menos afetado). Level 100k (log).
// =====================================================================================
class SD1 : public PedalBase
{
public:
    void setParams (float drive, float tone, float level)
    {
        if (drive != pDrive || tone != pTone || level != pLevel) { pDrive = drive; pTone = tone; pLevel = level; markDirty(); } // NOLINT
    }
protected:
    void prepareCircuit() override
    {
        inHp.set (0, 0.047e-6 * 470e3, 1, 0.047e-6 * 470e3, osFs);
        cplHp.set (0, 1e-6 * 10e3, 1, 1e-6 * 10e3, osFs);
        clip.prepare (osFs);
        clip.diodes.build (D1S2473, 2, 1);
        tone.clear();
        const int nIn = tone.node(), nX = tone.node(), nN = tone.node(), nW = tone.node(), nM = tone.node(), nO = tone.node();
        tone.setInput (nIn); tone.setOutput (nO);
        tone.addR (nIn, nX, 1e3);
        tone.addC (nX, 0, 0.15e-6);         // ~1,06 kHz
        potA = tone.addR (nX, nW, 10e3);
        potB = tone.addR (nW, nN, 10e3);
        tone.addR (nW, nM, 1e3);
        tone.addC (nM, 0, 0.1e-6);          // ~1,6 kHz
        tone.addR (nO, nN, 1e3);
        tone.addOpAmp (nX, nN, nO);
        tone.prepare (osFs);
        outHp.set (0, 1e-6 * 100e3, 1, 1e-6 * 100e3, osFs);
    }
    void resetCircuit() override { inHp.reset(); cplHp.reset(); clip.reset(); tone.reset(); outHp.reset(); }
    void updateCircuit() override
    {
        clip.set (4.7e3, 47e-9, 33e3 + 1e6 * audioTaper (pDrive / 10.0), 47e-12, 3.4);
        const double t = juce::jlimit (0.002, 0.998, pTone / 10.0);
        tone.setR (potA, 20e3 * t);
        tone.setR (potB, 20e3 * (1.0 - t));
        outGain = (float) audioTaper (pLevel / 10.0);
    }
    void processOS (float* x, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            float v = cplHp.process (inHp.process (x[i]));
            v = clip.process (v);
            v = tone.process (v);
            x[i] = outHp.process (3.4f * std::tanh (v / 3.4f));
        }
    }
private:
    vlv::tube::IIR1 inHp, cplHp, outHp;
    FeedbackClipper clip;
    LinearCircuit tone;
    int potA = 0, potB = 0;
    float pDrive = 5, pTone = 5, pLevel = 5;
};

// =====================================================================================
// GOLD DRIVE — Klon Centaur
//  Buffer TL072 -> divide em dois caminhos:
//   Sujo: amp-op não-inversor (trilhos de 18 V): Rf = 422k || 390 pF (pico ~1 kHz),
//         Rg = 15k + GAIN1 (100k) + 82 nF. -> 1 uF -> 1k -> diodos 1N34A para a terra.
//   Limpo: 1k5 + 1 uF (passa-baixa 106 Hz) e um caminho controlado por GAIN2
//          (quanto mais ganho, menos limpo).
//  Somador inversor: 392k || 820 pF (495 Hz). Treble ativo: canto 408 Hz, +18 dB / -8 dB.
//  Output 10k (log).
// =====================================================================================
class Klon : public PedalBase
{
public:
    void setParams (float gain, float treble, float output)
    {
        if (gain != pGain || treble != pTreble || output != pOut) { pGain = gain; pTreble = treble; pOut = output; markDirty(); } // NOLINT
    }
protected:
    void prepareCircuit() override
    {
        inHp.set (0, 0.1e-6 * 1e6, 1, 0.1e-6 * 1e6, osFs);             // C1 100 nF / R2 1M
        // Estágio de ganho
        gainSt.clear();
        {
            const int nIn = gainSt.node(), nP = gainSt.node(), nM = gainSt.node(), nG = gainSt.node(), nO = gainSt.node();
            gainSt.setInput (nIn); gainSt.setOutput (nO);
            gainSt.addC (nIn, nP, 68e-9);                                // acoplamento
            gainSt.addR (nP, 0, 10e3);                                   // polarização
            gainSt.addR (nO, nM, 422e3);                                 // R12
            gainSt.addC (nO, nM, 390e-12);                               // C8
            rgIdx = gainSt.addR (nM, nG, 15e3 + 50e3);                   // R11 + GAIN1
            gainSt.addC (nG, 0, 82e-9);                                  // C7
            gainSt.addOpAmp (nP, nM, nO);
            gainSt.prepare (osFs);
        }
        clipCpl.set (0, 1e-6 * 1e3, 1, 1e-6 * 1e3, osFs);
        clipper.set (1e3);
        clipper.diodes.build (D1N34A, 1, 1);
        // Caminho limpo 1: 1k5 + 1 uF
        cleanLp.set (1, 0, 1, (1.5e3 * 15e3 / 16.5e3) * 1e-6, osFs);
        // Somador
        sum.clear();
        {
            const int nD = sum.node(), nM = sum.node(), nO = sum.node();
            sum.setInput (nD); sum.setOutput (nO);
            sum.addR (nD, nM, 22e3);
            sum.addR (nO, nM, 392e3);
            sum.addC (nO, nM, 820e-12);
            sum.addOpAmp (0, nM, nO);
            sum.prepare (osFs);
        }
        sumLp.set (1, 0, 1, 392e3 * 820e-12, osFs);
        outHp.set (0, 4.7e-6 * 10e3, 1, 4.7e-6 * 10e3, osFs);
    }
    void resetCircuit() override { inHp.reset(); gainSt.reset(); clipCpl.reset(); clipper.reset(); cleanLp.reset(); sum.reset(); sumLp.reset(); treble.reset(); outHp.reset(); }
    void updateCircuit() override
    {
        const double g = juce::jlimit (0.0, 1.0, pGain / 10.0);
        gainSt.setR (rgIdx, 15e3 + 100e3 * (1.0 - g));
        // Ganho dos caminhos limpos no somador (392k / R de entrada)
        // caminho limpo 1 (1k5 -> 1 uF -> 15k): divisor 15k/16k5 e ganho 392k/15k
        cleanGain1 = (float) (392e3 / 15e3 * 15e3 / 16.5e3);
        // caminho limpo 2 (GAIN2): quanto mais ganho, maior a resistência e menos limpo
        cleanGain2 = (float) (392e3 / (27e3 + 1e6 * audioTaper (g)));
        // Treble: shelf ativo, canto 408 Hz; ganho em alta de 0,4 (-8 dB) a 8,16 (+18 dB)
        const double tr = juce::jlimit (0.0, 1.0, pTreble / 10.0);
        const double Ghf = (10e3 * tr + 4.7e3) / (1.8e3 + 10e3 * (1.0 - tr));
        const double tau = 100e3 * 3.9e-9;
        treble.set (1.0, Ghf * tau, 1.0, tau, osFs);
        outGain = (float) audioTaper (pOut / 10.0);
    }
    void processOS (float* x, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            const float in = inHp.process (x[i]);
            // caminho sujo: estágio de ganho -> diodos de germânio para a terra
            float d = gainSt.process (in);
            d = 8.5f * std::tanh (d / 8.5f);                             // trilhos de 18 V
            d = clipper.process (clipCpl.process (d));
            // somador inversor (terra virtual): cada caminho entra com ganho 392k / R de entrada,
            // e a realimentação 392k || 820 pF faz o passa-baixa de 495 Hz
            const float u = (float) (392e3 / 22e3) * d
                          + cleanGain1 * cleanLp.process (in)
                          + cleanGain2 * in;
            float s = -sumLp.process (u);
            s = 8.5f * std::tanh (s / 8.5f);
            s = treble.process (s);
            x[i] = outHp.process (8.5f * std::tanh (s / 8.5f));
        }
    }
private:
    vlv::tube::IIR1 inHp, clipCpl, cleanLp, sumLp, treble, outHp;
    LinearCircuit gainSt, sum;
    ShuntClipper clipper;
    int rgIdx = 0;
    float cleanGain1 = 1, cleanGain2 = 1;
    float pGain = 3, pTreble = 5, pOut = 5;
};

// =====================================================================================
// BLUE DRIVE — Analogman King of Tone (um lado; topologia Bluesbreaker com os valores do KoT)
//  Estágio 1 (não-inversor): Rf = parte do pot Gain 100k (B) || 100 pF;
//                            Rg = 33k || (27k + 100 nF)  [valores do KoT]
//  Estágio 2 (inversor): Rin = 10k + resto do pot Gain, acoplamento 1 uF, Rf = 220k;
//      modo Overdrive: 2x 1N4148 em série em cada sentido + 6k8 na realimentação.
//  Tone passivo: 6k8 + pot Tone 25k (B) + 10 nF; Presence (50k A) + 10 nF em paralelo.
//  Volume 100k (A). Alimentação 9 V (padrão).
// =====================================================================================
class KingOfTone : public PedalBase
{
public:
    void setParams (float drive, float tone, float volume)
    {
        if (drive != pDrive || tone != pTone || volume != pVol) { pDrive = drive; pTone = tone; pVol = volume; markDirty(); } // NOLINT
    }
protected:
    void prepareCircuit() override
    {
        inHp.set (0, 10e-9 * 1e6, 1, 10e-9 * 1e6, osFs);
        st1.clear();
        {
            const int nIn = st1.node(), nM = st1.node(), nA = st1.node(), nO = st1.node();
            st1.setInput (nIn); st1.setOutput (nO);
            rfIdx = st1.addR (nO, nM, 50e3);
            st1.addC (nO, nM, 100e-12);
            st1.addR (nM, 0, 33e3);
            st1.addR (nM, nA, 27e3);
            st1.addC (nA, 0, 100e-9);
            st1.addOpAmp (nIn, nM, nO);
            st1.prepare (osFs);
        }
        st2.prepare (osFs);
        st2.diodes.build (D1N914, 2, 2, 6.8e3);
        tone.clear();
        {
            const int nIn = tone.node(), nT = tone.node(), nP = tone.node(), nC = tone.node();
            tone.setInput (nIn); tone.setOutput (nT);
            tone.addR (nIn, nT, 6.8e3);
            presIdx = tone.addR (nIn, nP, 25e3);         // presence pot
            tone.addC (nP, nT, 10e-9);
            toneIdx = tone.addR (nT, nC, 12e3);          // tone pot
            tone.addC (nC, 0, 10e-9);
            tone.addR (nT, 0, 100e3);                    // volume pot (carga)
            tone.prepare (osFs);
        }
        outHp.set (0, 1e-6 * 100e3, 1, 1e-6 * 100e3, osFs);
    }
    void resetCircuit() override { inHp.reset(); st1.reset(); st2.reset(); tone.reset(); outHp.reset(); }
    void updateCircuit() override
    {
        const double a = juce::jlimit (0.0, 1.0, pDrive / 10.0);   // pot linear (B)
        st1.setR (rfIdx, 100e3 * a + 10.0);
        st2.set (10e3 + 100e3 * (1.0 - a), 1e-6, 220e3, 100e-12, 3.6);
        tone.setR (toneIdx, 25e3 * (pTone / 10.0) + 50.0);
        tone.setR (presIdx, 50e3 * (1.0 - audioTaper (0.25)) + 100.0);   // presence interno em ~25%
        outGain = (float) audioTaper (pVol / 10.0) * 1.4f;
    }
    void processOS (float* x, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            float v = st1.process (inHp.process (x[i]));
            v = 3.6f * std::tanh (v / 3.6f);
            v = st2.process (v);
            v = tone.process (v);
            x[i] = outHp.process (v);
        }
    }
private:
    vlv::tube::IIR1 inHp, outHp;
    LinearCircuit st1, tone;
    InvertingClipper st2;
    int rfIdx = 0, toneIdx = 0, presIdx = 0;
    float pDrive = 5, pTone = 5, pVol = 5;
};

// =====================================================================================
// SILVER COMP — compressor no estilo Keeley C4 (base Ross/Dyna Comp, OTA CA3080)
//  OTA: Iout = Iabc * tanh(Vd / 2Vt) — o knob CLIP aumenta o sinal na entrada do OTA
//  (mais saturação do próprio OTA). Detector de pico na saída reduz Iabc (compressão por
//  realimentação). SUSTAIN = ganho máximo do OTA; ATTACK = tempo de carga do detector;
//  release fixo como no Ross. LEVEL = volume de saída.
// =====================================================================================
class OTACompressor : public PedalBase
{
public:
    void setParams (float sustain, float level, float attack, float clip)
    {
        if (sustain != pSus || level != pLev || attack != pAtk || clip != pClip) // NOLINT
        { pSus = sustain; pLev = level; pAtk = attack; pClip = clip; markDirty(); }
    }
protected:
    void prepareCircuit() override
    {
        inHp.set (0, 0.1e-6 * 500e3, 1, 0.1e-6 * 500e3, osFs);
        outLp.lowpass (osFs, 14000.0, 0.6);
        release = (float) std::exp (-1.0 / (0.45 * osFs));
    }
    void resetCircuit() override { inHp.reset(); outLp.reset(); env = 0.0f; }
    void updateCircuit() override
    {
        imax = (float) std::pow (10.0, 1.5 * pSus / 10.0);           // ganho máximo do OTA (Sustain): 0 a +30 dB
        inScale = (float) std::pow (10.0, (pClip - 3.0) / 10.0 * 1.2); // nível na entrada do OTA (Clip): 3 = nominal
        const double atkSec = 0.0008 + 0.045 * (pAtk / 10.0);
        attack = (float) (1.0 - std::exp (-1.0 / (atkSec * osFs)));
        outGain = (float) std::pow (10.0, (-12.0 + (pLev - 5.0) * 4.0) / 20.0);   // Level 5 = volume parecido com o bypass
    }
    void processOS (float* x, int n) override
    {
        const float vth = 0.18f;        // limiar do detector (Vbe do transistor detector, referido à saída)
        for (int i = 0; i < n; ++i)
        {
            const float vin = inHp.process (x[i]) * inScale;
            const float iabc = imax / (1.0f + juce::jmax (0.0f, env - vth) * 18.0f);
            // OTA: entrada diferencial atenuada; saturação tanh do par diferencial
            float y = iabc * 0.5f * std::tanh (vin / 0.5f);          // par diferencial do OTA (faixa linear ~0,5 V referida à entrada)
            y = outLp.process (y);
            const float a = std::abs (y);
            env = a > env ? env + (a - env) * attack : env * release;
            x[i] = y;
        }
    }
private:
    vlv::tube::IIR1 inHp;
    Biquad outLp;
    float env = 0.0f, attack = 0.01f, release = 0.9999f, imax = 3.0f, inScale = 1.0f;
    float pSus = 5, pLev = 5, pAtk = 5, pClip = 3;
};
} // namespace vlv::pedal

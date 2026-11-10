#pragma once
// =====================================================================================
// Motor "Schematic": Marshall JCM800 2203 (100 W, Lead Master Volume) simulado a partir do
// esquema original (Marshall drawing 2203 STD, iss. 4/6), componente por componente.
//
// Cada parte do circuito é um sistema nodal (MNA) resolvido amostra a amostra pelo método de
// Newton, com capacitores e indutores integrados pela regra trapezoidal. As válvulas usam os
// modelos de Koren (12AX7/ECC83 e EL34), com corrente de grade e as capacitâncias internas
// (Cgk, Cgp, Cpk) das fichas das válvulas.
//
// Blocos (separados onde a interação é desprezível: saída de placa -> rede de 1 MΩ):
//   A) Entrada High (R2 1M, R3 68k) + V1a (R4 100k, R1 2k7 || C1 0.68µ, C2 100p placa-catodo)
//   B) C3 22n -> R5 470k || C4 470p -> VR1 1M log (Preamp Vol) com C5 1n (bright) -> V1b
//      (R7 100k, R6 10k sem bypass)
//   C) C7 22n -> R10 470k || C8 470p -> R11 470k -> V2a (R12 100k, R9 820) -> V2b seguidor de
//      catodo (R13 100k), acoplado direto
//   D) Tone stack: C10 470p, R15 33k, C11 22n, C12 22n, Treble 220k lin, Bass 1M log,
//      Middle 22k lin -> Master VR2 1M log
//   E) Inversor de fase V3 (long-tail: R16 470, R20 10k, R23 4k7, R17/R19 1M, C15 0.1µ,
//      placas R18 82k / R21 100k), C14/C16 22n, C18 47p, R24/R25 220k para o bias,
//      grid stoppers 5k6, 4x EL34 em push-pull (polarização fixa), transformador de saída,
//      realimentação R22 100k com Presence VR6 22k lin + C17 0.1µ
// Fonte: B+ 465 V nas placas; nós X e do V1 calculados pelos resistores R29 10k e R8 10k e pela
// corrente real de cada válvula.
// =====================================================================================
#include <cmath>
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>

namespace vlv::sch
{
// ---------- Válvulas ----------
struct TriodeEval { double ip, gm, gp; };   // corrente de placa e derivadas (dIp/dVgk, dIp/dVpk)

// 12AX7 / ECC83 (Koren): mu 100, ex 1.4, kg1 1060, kp 600, kvb 300
inline TriodeEval ecc83 (double vgk, double vpk)
{
    constexpr double mu = 100.0, ex = 1.4, kg1 = 1060.0, kp = 600.0, kvb = 300.0;
    // tensão de placa suavizada perto de 0 V (sem degrau na derivada)
    double dv = 1.0;
    { const double a = vpk / 2.0; if (a < 30.0) { const double e = std::exp (a); vpk = 2.0 * std::log1p (e) + 1e-6; dv = e / (1.0 + e); } }
    const double S = std::sqrt (kvb + vpk * vpk);
    const double u = kp * (1.0 / mu + vgk / S);
    double L, sig;
    if (u > 30.0) { L = u; sig = 1.0; }
    else if (u < -30.0) { L = std::exp (u); sig = L; }
    else { const double e = std::exp (u); L = std::log1p (e); sig = e / (1.0 + e); }
    const double E1 = vpk / kp * L;
    if (E1 <= 1e-12) return { 0.0, 0.0, 0.0 };
    const double dE1dg = vpk * sig / S;
    const double dE1dp = L / kp - sig * vgk * vpk * vpk / (S * S * S);
    const double p = std::pow (E1, ex - 1.0);
    const double ip = 2.0 * p * E1 / kg1;
    const double k = 2.0 * ex * p / kg1;
    return { ip, k * dE1dg, k * dE1dp * dv };
}

// EL34 (Koren pentodo): mu 11, ex 1.35, kg1 650, kp 60, kvb 24; grade 2 na tensão de tela
inline TriodeEval el34 (double vg1k, double vpk, double vg2k)
{
    constexpr double mu = 11.0, ex = 1.35, kg1 = 650.0, kp = 60.0, kvb = 24.0;
    const double u = kp * (1.0 / mu + vg1k / vg2k);
    double L, sig;
    if (u > 30.0) { L = u; sig = 1.0; }
    else if (u < -30.0) { L = std::exp (u); sig = L; }
    else { const double e = std::exp (u); L = std::log1p (e); sig = e / (1.0 + e); }
    const double E1 = vg2k / kp * L;
    if (E1 <= 1e-12) return { 0.0, 0.0, 0.0 };
    // tensão de placa suavizada perto de 0 V (evita o "degrau" que trava o Newton)
    double vp, dvp;
    { const double a = vpk / 5.0; if (a > 30.0) { vp = vpk; dvp = 1.0; } else { const double e = std::exp (a); vp = 5.0 * std::log1p (e); dvp = e / (1.0 + e); } }
    const double at = std::atan (vp / kvb);
    const double p = std::pow (E1, ex - 1.0);
    const double base = 2.0 * p * E1 / kg1;
    const double ip = base * at;
    const double gm = 2.0 * ex * p / kg1 * sig * at;
    const double gp = base * (1.0 / kvb) / (1.0 + (vp / kvb) * (vp / kvb)) * dvp;
    return { ip, gm, gp };
}

// Corrente de grade (grade positiva em relação ao catodo): diodo suave
struct GridEval { double i, g; };
inline GridEval gridCurrent (double vgk, double k)
{
    // softplus com joelho de 0,1 V, depois cresce com expoente 1,5 (lei de Child-Langmuir)
    const double a = vgk / 0.1;
    double sp, dsp;
    if (a > 30.0) { sp = vgk; dsp = 1.0; }
    else { const double e = std::exp (a); sp = 0.1 * std::log1p (e); dsp = e / (1.0 + e); }
    const double r = std::sqrt (sp);
    return { k * sp * r, k * 1.5 * r * dsp };
}

// Capacitâncias internas (fichas): ECC83 Cgk 1.6 pF, Cgp 1.7 pF, Cpk 0.46 pF
constexpr double ecc83Cgk = 1.6e-12, ecc83Cgp = 1.7e-12, ecc83Cpk = 0.46e-12;
constexpr double kGrid12AX7 = 6e-4, kGridEL34 = 2e-3;

// Potenciômetro logarítmico (curva "A" padrão: 10% da resistência no meio do curso)
inline double logTaper (double x) { x = std::clamp (x, 0.0, 1.0); return (std::pow (10.0, 2.0 * x) - 1.0) / 99.0; }

// ---------- Solucionador nodal (MNA) de N nós ----------
template <int N>
struct Mna
{
    double G[N][N] {};        // parte linear constante (resistores + modelos companheiros)
    double v[N] {};           // tensões dos nós (solução atual)
    double rhs[N] {};         // fontes da amostra atual
    double vPrev[N] {};       // solução da amostra anterior (para o chute inicial)
    double stepLimit[N];      // maior passo de Newton por nó (V): grades/catodos 10 V, placas mais
    Mna() { for (auto& l : stepLimit) l = 10.0; }

    struct Cap { int a, b; double g, j; };     // companheiro trapezoidal: corrente = g*v - j
    std::array<Cap, 24> caps {}; int nCaps = 0;
    struct Ind { int a, b; double g, j; };     // indutor: corrente = g*v + j
    std::array<Ind, 4> inds {}; int nInds = 0;

    void clear() { std::memset (G, 0, sizeof (G)); nCaps = 0; nInds = 0; std::memset (v, 0, sizeof (v)); }
    void res (int a, int b, double r) { cond (a, b, 1.0 / r); }
    void cond (int a, int b, double g)
    {
        if (a >= 0) G[a][a] += g;
        if (b >= 0) G[b][b] += g;
        if (a >= 0 && b >= 0) { G[a][b] -= g; G[b][a] -= g; }
    }
    void cap (int a, int b, double c, double T) { cond (a, b, 2.0 * c / T); caps[(size_t) nCaps++] = { a, b, 2.0 * c / T, 0.0 }; }
    void ind (int a, int b, double l, double T) { cond (a, b, T / (2.0 * l)); inds[(size_t) nInds++] = { a, b, T / (2.0 * l), 0.0 }; }

    double vd (int a, int b) const { return (a >= 0 ? v[a] : 0.0) - (b >= 0 ? v[b] : 0.0); }

    // ---- Redução de Schur: os nós lineares são eliminados uma vez (quando a matriz muda) e o
    //      Newton trabalha só nos nós ligados às válvulas ----
    int nl[N] {}, ln[N] {}; int nNl = 0, nLin = 0;
    double Gred[N][N] {};     // nNl x nNl
    double Mr[N][N] {};       // nNl x nLin   (G12 * inv(G22))
    double Hl[N][N] {};       // nLin x nNl   (inv(G22) * G21)
    double Il[N][N] {};       // nLin x nLin  (inv(G22))
    void setNonlinear (std::initializer_list<int> nodes)
    {
        nNl = 0; nLin = 0;
        bool isNl[N] {};
        for (int n : nodes) isNl[n] = true;
        for (int i = 0; i < N; ++i) { if (isNl[i]) nl[nNl++] = i; else ln[nLin++] = i; }
    }
    void reduce()
    {
        // inversa de G22 (Gauss-Jordan)
        double a[N][2 * N] {};
        for (int r = 0; r < nLin; ++r)
        {
            for (int c = 0; c < nLin; ++c) a[r][c] = G[ln[r]][ln[c]];
            a[r][nLin + r] = 1.0;
        }
        for (int c = 0; c < nLin; ++c)
        {
            int p = c;
            for (int r = c + 1; r < nLin; ++r) if (std::abs (a[r][c]) > std::abs (a[p][c])) p = r;
            if (p != c) for (int k = 0; k < 2 * nLin; ++k) std::swap (a[c][k], a[p][k]);
            const double inv = 1.0 / a[c][c];
            for (int k = 0; k < 2 * nLin; ++k) a[c][k] *= inv;
            for (int r = 0; r < nLin; ++r)
            {
                if (r == c || a[r][c] == 0.0) continue;
                const double f = a[r][c];
                for (int k = 0; k < 2 * nLin; ++k) a[r][k] -= f * a[c][k];
            }
        }
        for (int r = 0; r < nLin; ++r) for (int c = 0; c < nLin; ++c) Il[r][c] = a[r][nLin + c];
        for (int r = 0; r < nNl; ++r)
            for (int c = 0; c < nLin; ++c)
            {
                double s = 0.0;
                for (int k = 0; k < nLin; ++k) s += G[nl[r]][ln[k]] * Il[k][c];
                Mr[r][c] = s;
            }
        for (int r = 0; r < nLin; ++r)
            for (int c = 0; c < nNl; ++c)
            {
                double s = 0.0;
                for (int k = 0; k < nLin; ++k) s += Il[r][k] * G[ln[k]][nl[c]];
                Hl[r][c] = s;
            }
        for (int r = 0; r < nNl; ++r)
            for (int c = 0; c < nNl; ++c)
            {
                double s = G[nl[r]][nl[c]];
                for (int k = 0; k < nLin; ++k) s -= Mr[r][k] * G[ln[k]][nl[c]];
                Gred[r][c] = s;
            }
    }

    // Fontes de histórico dos capacitores/indutores
    void addHistory()
    {
        for (int i = 0; i < nCaps; ++i)
        {
            const auto& c = caps[(size_t) i];
            if (c.a >= 0) rhs[c.a] += c.j;
            if (c.b >= 0) rhs[c.b] -= c.j;
        }
        for (int i = 0; i < nInds; ++i)
        {
            const auto& l = inds[(size_t) i];
            if (l.a >= 0) rhs[l.a] -= l.j;
            if (l.b >= 0) rhs[l.b] += l.j;
        }
    }
    void updateHistory()
    {
        for (int i = 0; i < nCaps; ++i)
        {
            auto& c = caps[(size_t) i];
            const double vc = vd (c.a, c.b);
            const double ic = c.g * vc - c.j;
            c.j = c.g * vc + ic;
        }
        for (int i = 0; i < nInds; ++i)
        {
            auto& l = inds[(size_t) i];
            const double vl = vd (l.a, l.b);
            const double il = l.g * vl + l.j;
            l.j = il + l.g * vl;
        }
    }
    // Estado de repouso coerente (capacitores carregados, sem corrente)
    void settleHistory()
    {
        std::memcpy (vPrev, v, sizeof (v));
        for (int i = 0; i < nCaps; ++i) { auto& c = caps[(size_t) i]; c.j = c.g * vd (c.a, c.b); }
        for (int i = 0; i < nInds; ++i) { auto& l = inds[(size_t) i]; l.j = 0.0; }
    }

    // Resolve A x = b (eliminação de Gauss com pivotamento parcial); A e b são destruídos
    static bool solve (double (&A)[N][N], double (&b)[N])
    {
        for (int c = 0; c < N; ++c)
        {
            int p = c; double best = std::abs (A[c][c]);
            for (int r = c + 1; r < N; ++r) if (std::abs (A[r][c]) > best) { best = std::abs (A[r][c]); p = r; }
            if (best < 1e-300) return false;
            if (p != c) { for (int k = 0; k < N; ++k) std::swap (A[c][k], A[p][k]); std::swap (b[c], b[p]); }
            const double inv = 1.0 / A[c][c];
            for (int r = c + 1; r < N; ++r)
            {
                const double f = A[r][c] * inv;
                if (f == 0.0) continue;
                for (int k = c; k < N; ++k) A[r][k] -= f * A[c][k];
                b[r] -= f * b[c];
            }
        }
        for (int r = N - 1; r >= 0; --r)
        {
            double s = b[r];
            for (int k = r + 1; k < N; ++k) s -= A[r][k] * b[k];
            b[r] = s / A[r][r];
        }
        return true;
    }
};

// Aplica uma válvula triodo (g, k, p) no Jacobiano/resíduo
template <int N>
inline void stampTriode (double (&J)[N][N], double (&F)[N], const double (&v)[N], int g, int k, int p, double kGrid)
{
    const double vg = g >= 0 ? v[g] : 0.0, vk = k >= 0 ? v[k] : 0.0, vp = p >= 0 ? v[p] : 0.0;
    const auto t = ecc83 (vg - vk, vp - vk);
    // corrente de placa sai do nó p e entra no nó k
    if (p >= 0) { F[p] += t.ip; if (g >= 0) J[p][g] += t.gm; J[p][p] += t.gp; if (k >= 0) J[p][k] -= t.gm + t.gp; }
    if (k >= 0) { F[k] -= t.ip; if (g >= 0) J[k][g] -= t.gm; if (p >= 0) J[k][p] -= t.gp; J[k][k] += t.gm + t.gp; }
    const auto gc = gridCurrent (vg - vk, kGrid);
    if (g >= 0) { F[g] += gc.i; J[g][g] += gc.g; if (k >= 0) J[g][k] -= gc.g; }
    if (k >= 0) { F[k] -= gc.i; if (g >= 0) J[k][g] -= gc.g; J[k][k] += gc.g; }
}

inline long long& newtonFailures() { static long long n = 0; return n; }
inline double& newtonWorst() { static double w = 0; return w; }
inline int& newtonWhere() { static int w = 0; return w; }

// Newton no sistema reduzido: Gred*vn + correntes das válvulas = rhs reduzido.
// O lambda dos dispositivos recebe matrizes N x N (índices de nó completos).
template <int N, class Dev>
inline void newton (Mna<N>& m, Dev&& devices, int maxIt = 50, double tol = 5e-4)
{
    const int M = m.nNl;
    double rr[N];
    for (int r = 0; r < M; ++r)
    {
        double s = m.rhs[m.nl[r]];
        for (int k = 0; k < m.nLin; ++k) s -= m.Mr[r][k] * m.rhs[m.ln[k]];
        rr[r] = s;
    }
    // chute inicial: solução da amostra anterior
    bool converged = false;
    double lastStep = 0.0;
    for (int it = 0; it < maxIt; ++it)
    {
        double J[N][N], F[N];
        std::memset (J, 0, sizeof (J)); std::memset (F, 0, sizeof (F));
        devices (J, F, m.v);
        double Jr[N][N], Fr[N];
        for (int r = 0; r < M; ++r)
        {
            double s = -rr[r] + F[m.nl[r]];
            for (int c = 0; c < M; ++c) { Jr[r][c] = m.Gred[r][c] + J[m.nl[r]][m.nl[c]]; s += m.Gred[r][c] * m.v[m.nl[c]]; }
            Fr[r] = -s;

        }

        // resolve o sistema M x M
        for (int c = 0; c < M; ++c)
        {
            // pivotamento só quando o pivô é pequeno (a matriz nodal é quase diagonal dominante)
            double best = std::abs (Jr[c][c]); int p = c;
            double colMax = best;
            for (int r = c + 1; r < M; ++r) colMax = std::max (colMax, std::abs (Jr[r][c]));
            if (best < 0.1 * colMax)
                for (int r = c + 1; r < M; ++r) if (std::abs (Jr[r][c]) > best) { best = std::abs (Jr[r][c]); p = r; }
            if (p != c) { for (int k = 0; k < M; ++k) std::swap (Jr[c][k], Jr[p][k]); std::swap (Fr[c], Fr[p]); }
            const double inv = 1.0 / Jr[c][c];
            for (int r = c + 1; r < M; ++r)
            {
                const double f = Jr[r][c] * inv;
                if (f == 0.0) continue;
                for (int k = c; k < M; ++k) Jr[r][k] -= f * Jr[c][k];
                Fr[r] -= f * Fr[c];
            }
        }
        for (int r = M - 1; r >= 0; --r)
        {
            double s = Fr[r];
            for (int k = r + 1; k < M; ++k) s -= Jr[r][k] * Fr[k];
            Fr[r] = s / Jr[r][r];
        }
        double mx = 0.0, scale = 1.0;
        for (int r = 0; r < M; ++r)
        {
            const double a = std::abs (Fr[r]);
            mx = std::max (mx, a);
            const double lim = m.stepLimit[m.nl[r]];           // limita o passo (estabilidade)
            if (a * scale > lim) scale = lim / a;
        }
        if (it >= 12) scale *= (it % 3 == 0) ? 0.3 : 0.6;     // quebra ciclos (Newton oscilando entre 2 pontos)
        for (int r = 0; r < M; ++r) m.v[m.nl[r]] += scale * Fr[r];
        lastStep = mx;
        if (mx < tol) { converged = true; break; }
    }
    if (! converged) { ++newtonFailures(); newtonWorst() = std::max (newtonWorst(), lastStep); newtonWhere() = N; }
    // nós lineares
    for (int r = 0; r < m.nLin; ++r)
    {
        double s = 0.0;
        for (int k = 0; k < m.nLin; ++k) s += m.Il[r][k] * m.rhs[m.ln[k]];
        for (int c = 0; c < M; ++c) s -= m.Hl[r][c] * m.v[m.nl[c]];
        m.v[m.ln[r]] = s;
    }
}

// =====================================================================================
class Jcm800
{
public:
    // fsOS: taxa de amostragem interna (já sobreamostrada)
    void prepare (double fsOS)
    {
        T = 1.0 / fsOS;
        knobsDirty = true;
        build();
        operatingPoint();
    }

    // Knobs 0..10 (Preamp Vol, Bass, Middle, Treble, Presence, Master)
    void setKnobs (double gain, double bass, double mid, double treble, double presence, double master)
    {
        const double n[6] = { gain, bass, mid, treble, presence, master };
        for (int i = 0; i < 6; ++i)
            if (std::abs (n[i] - knob[i]) > 1e-6) { knob[i] = n[i]; knobsDirty = true; }
    }

    void reset() { build(); operatingPoint(); }

    // Entrada em volts no jack High; saída em volts no alto-falante (16 Ω)
    inline double process (double vin)
    {
        if (knobsDirty) applyKnobs();
        // ---- A: V1a ----
        {
            auto& m = A;
            std::memset (m.rhs, 0, sizeof (m.rhs));
            m.rhs[aIn] += vin * gSrc;
            m.rhs[aP] += vRail1 / 100e3;                // R4 ao B+ do V1
            m.addHistory();
            newton (m, [&] (auto& J, auto& F, const auto& v) { stampTriode (J, F, v, aG, aK, aP, kGrid12AX7); });
            m.updateHistory();
        }
        const double vp1 = A.v[aP];
        // ---- B: rede do Preamp Vol + V1b ----
        {
            auto& m = B;
            std::memset (m.rhs, 0, sizeof (m.rhs));
            m.rhs[bSrc] += vp1 * gSrc;
            m.rhs[bP] += vRail1 / 100e3;                // R7
            m.addHistory();
            newton (m, [&] (auto& J, auto& F, const auto& v) { stampTriode (J, F, v, bW, bK, bP, kGrid12AX7); });
            m.updateHistory();
        }
        const double vp2 = B.v[bP];
        // ---- C: V2a + seguidor de catodo V2b ----
        {
            auto& m = C;
            std::memset (m.rhs, 0, sizeof (m.rhs));
            m.rhs[cSrc] += vp2 * gSrc;
            m.rhs[cP] += vRailX / 100e3;                // R12 ao nó X
            m.addHistory();
            newton (m, [&] (auto& J, auto& F, const auto& v)
            {
                stampTriode (J, F, v, cG, cK, cP, kGrid12AX7);
                // V2b: grade = placa do V2a, placa no nó X (fixo), catodo cK2
                const double vg = v[cP], vk = v[cK2];
                const auto t = ecc83 (vg - vk, vRailX - vk);
                F[cK2] -= t.ip; J[cK2][cP] -= t.gm; J[cK2][cK2] += t.gm + t.gp;
                const auto gc = gridCurrent (vg - vk, kGrid12AX7);
                F[cP] += gc.i; J[cP][cP] += gc.g; J[cP][cK2] -= gc.g;
                F[cK2] -= gc.i; J[cK2][cP] -= gc.g; J[cK2][cK2] += gc.g;
            });
            m.updateHistory();
        }
        const double vcf = C.v[cK2] - cfDc;              // saída do seguidor (o DC é bloqueado pelos caps do tone stack)
        // ---- D: tone stack + master (linear) ----
        {
            auto& m = D;
            std::memset (m.rhs, 0, sizeof (m.rhs));
            m.rhs[dSrc] += vcf * gSrc;
            m.addHistory();
            newton (m, [] (auto&, auto&, const auto&) {}, 1, 1e30);
            m.updateHistory();
        }
        const double vMaster = D.v[dW];
        // ---- E: inversor de fase + potência + transformador + realimentação ----
        {
            auto& m = E;
            std::memset (m.rhs, 0, sizeof (m.rhs));
            m.rhs[eSrc] += vMaster * gSrc;
            m.rhs[ePa] += vRailX / 82e3;                  // R18
            m.rhs[ePb] += vRailX / 100e3;                 // R21
            m.rhs[eGa] += vBias / 220e3;                  // R24 ao bias
            m.rhs[eGb] += vBias / 220e3;                  // R25
            // transformador: modo comum preso ao B+ (center tap)
            m.rhs[ePA] += gCm * vPlate; m.rhs[ePB] += gCm * vPlate;
            m.addHistory();
            newton (m, [&] (auto& J, auto& F, const auto& v)
            {
                stampTriode (J, F, v, eG1, eK, ePa, kGrid12AX7);
                stampTriode (J, F, v, eG2, eK, ePb, kGrid12AX7);
                // 2x EL34 por lado (catodo no terra, tela fixa)
                for (int side = 0; side < 2; ++side)
                {
                    const int g = side == 0 ? eGA : eGB, p = side == 0 ? ePA : ePB;
                    const auto t = el34 (v[g], v[p], vScreen);
                    F[p] += 2.0 * t.ip; J[p][g] += 2.0 * t.gm; J[p][p] += 2.0 * t.gp;
                    const auto gc = gridCurrent (v[g], kGridEL34);
                    F[g] += 2.0 * gc.i; J[g][g] += 2.0 * gc.g;
                }
            });
            m.updateHistory();
        }
        // ---- Fonte: corrente das EL34 (placa + ~8% de tela) puxa o B+ ----
        {
            const double ia = el34 (E.v[eGA], E.v[ePA], vScreen).ip, ib = el34 (E.v[eGB], E.v[ePB], vScreen).ip;
            const double iTot = 2.0 * (ia + ib) * 1.08 + iPre;
            vPlate += T / Csup * ((vNoLoad - vPlate) / Rs - iTot);
            vScreen = vPlate - chokeDrop;
            // nós X e V1: filtrados por R29/C22 e R8/C21 (constante de ~0,5 s)
            const double tx = vScreen - dropX, t1 = vScreen - dropV1;
            vRailX += railLp * (tx - vRailX);
            vRail1 += railLp * (t1 - vRail1);
        }
        // tensão no secundário (16 Ω): (PB - PA) / relação de espiras
        return fbSign * (E.v[ePB] - E.v[ePA]) / turns;
    }

    // Diagnóstico
    double railX() const { return vRailX; }
    double rail1() const { return vRail1; }
    double bias() const { return vBias; }
    double idleEL34mA() const { return idleMa; }
    // nós para diagnóstico: 0 vCF, 1 master, 2 g1-k (PI), 3 grade EL34 A, 4 grade EL34 B, 5 placa A, 6 placa B, 7 B+
    double probe (int i) const
    {
        switch (i)
        {
            case 0: return C.v[cK2]; case 1: return D.v[dW]; case 2: return E.v[eG1] - E.v[eK];
            case 3: return E.v[eGA]; case 4: return E.v[eGB]; case 5: return E.v[ePA]; case 6: return E.v[ePB];
            default: return vPlate;
        }
    }

private:
    // índices dos nós
    enum { aIn = 0, aG, aK, aP, aO };                       // A: 5 nós
    enum { bSrc = 0, bN1, bN2, bW, bK, bP, bO };            // B: 7 nós
    enum { cSrc = 0, cN3, cG, cK, cP, cK2 };                // C: 6 nós
    enum { dSrc = 0, dT1, dTW, dTB, dMT, dM, dW };          // D: 7 nós
    enum { eSrc = 0, eG1, eG2, eK, eA, eB, ePa, ePb, eGa, eGb, eGA, eGB, ePA, ePB, eP };   // E: 15 nós

    static constexpr double gSrc = 1.0;      // fonte ideal modelada como Norton de 1 Ω
    static constexpr double gCm = 10.0;      // acoplamento de modo comum do transformador
    // Fonte: B+ de 465 V nas placas em repouso (antes do choke), telas 5 V abaixo (depois do choke).
    // A retificação + transformador de força têm impedância Rs; o filtro C23 segura o B+ (queda/"sag").
    static constexpr double vPlateIdle = 465.0, chokeDrop = 5.0, Rs = 100.0, Csup = 50e-6;
    double vPlate = vPlateIdle, vScreen = vPlateIdle - chokeDrop, vNoLoad = 480.0, iPre = 0.0;
    double dropX = 79.0, dropV1 = 91.0, railLp = 0.0;
    static constexpr double Raa = 1700.0, Rload = 16.0;        // primário placa-placa / secundário 16 Ω
    const double turns = std::sqrt (Raa / Rload);
    double fbSign = -1.0;     // fase do secundário: realimentação negativa

    double T = 1.0 / 192000.0;
    double knob[6] { 5, 5, 5, 5, 5, 5 };
    bool knobsDirty = true;
    double vRail1 = 330.0, vRailX = 400.0, vBias = -38.0, cfDc = 0.0, idleMa = 0.0;

    Mna<5> A; Mna<7> B; Mna<6> C; Mna<7> D; Mna<15> E;
    // pots (valores que dependem dos knobs) ficam numa matriz separada para remontar rápido
    double gainPos = 0.5, masterPos = 0.5;

    void buildA()
    {
        auto& m = A; m.clear();
        m.cond (aIn, -1, gSrc);
        m.res (aIn, -1, 1e6);                  // R2 1M (entrada)
        m.res (aIn, aG, 68e3);                 // R3 68k
        m.res (aK, -1, 2.7e3);                 // R1
        m.cap (aK, -1, 0.68e-6, T);            // C1
        m.cap (aP, aK, 100e-12, T);            // C2 placa-catodo
        m.res (aP, -1, 100e3);                 // R4 (ao B+, fonte no rhs)
        m.cap (aG, aK, ecc83Cgk, T); m.cap (aG, aP, ecc83Cgp, T); m.cap (aP, aK, ecc83Cpk, T);
        // carga da placa: C3 + rede seguinte (R5 + VR1 ~ 1,47 MΩ)
        m.cap (aP, aO, 22e-9, T);
        m.res (aO, -1, 1.47e6);
        m.setNonlinear ({ aG, aK, aP }); m.stepLimit[aP] = 100.0; m.reduce();
    }
    void buildB()
    {
        auto& m = B; m.clear();
        m.cond (bSrc, -1, gSrc);
        m.cap (bSrc, bN1, 22e-9, T);           // C3
        m.res (bN1, bN2, 470e3);               // R5
        m.cap (bN1, bN2, 470e-12, T);          // C4
        const double rb = std::max (1.0, 1e6 * logTaper (gainPos));
        const double rt = std::max (1.0, 1e6 - rb);
        m.res (bN2, bW, rt);                   // VR1 (topo -> cursor)
        m.res (bW, -1, rb);                    // VR1 (cursor -> terra)
        m.cap (bN2, bW, 1e-9, T);              // C5 1n (bright)
        m.res (bK, -1, 10e3);                  // R6
        m.res (bP, -1, 100e3);                 // R7
        m.cap (bW, bK, ecc83Cgk, T); m.cap (bW, bP, ecc83Cgp, T); m.cap (bP, bK, ecc83Cpk, T);
        m.cap (bP, bO, 22e-9, T);              // carga: C7 + R10 + R11
        m.res (bO, -1, 940e3);
        m.setNonlinear ({ bW, bK, bP }); m.stepLimit[bP] = 100.0; m.reduce();
    }
    void buildC()
    {
        auto& m = C; m.clear();
        m.cond (cSrc, -1, gSrc);
        m.cap (cSrc, cN3, 22e-9, T);           // C7
        m.res (cN3, cG, 470e3);                // R10
        m.cap (cN3, cG, 470e-12, T);           // C8
        m.res (cG, -1, 470e3);                 // R11
        m.res (cK, -1, 820.0);                 // R9
        m.res (cP, -1, 100e3);                 // R12 (ao nó X, fonte no rhs)
        m.res (cK2, -1, 100e3);                // R13
        m.cap (cG, cK, ecc83Cgk, T); m.cap (cG, cP, ecc83Cgp, T); m.cap (cP, cK, ecc83Cpk, T);
        m.cap (cP, cK2, ecc83Cgk, T); m.cap (cP, -1, ecc83Cgp, T);   // V2b: grade-catodo, grade-placa (placa = X)
        m.setNonlinear ({ cG, cK, cP, cK2 }); m.stepLimit[cP] = 100.0; m.stepLimit[cK2] = 100.0; m.reduce();
    }
    void buildD()
    {
        auto& m = D; m.clear();
        m.cond (dSrc, -1, gSrc);
        const double t = std::clamp (knob[3] / 10.0, 0.0, 1.0);
        const double l = std::clamp (knob[1] / 10.0, 0.0, 1.0);
        const double md = std::clamp (knob[2] / 10.0, 0.0, 1.0);
        m.cap (dSrc, dT1, 470e-12, T);                       // C10
        m.res (dT1, dTW, std::max (1.0, 220e3 * (1.0 - t))); // Treble (topo -> cursor)
        m.res (dTW, dTB, std::max (1.0, 220e3 * t));         // Treble (cursor -> base)
        m.res (dTB, dMT, std::max (1.0, 1e6 * logTaper (l)));// Bass 1M log (resistor variável)
        m.res (dMT, -1, std::max (1.0, 22e3 * md));          // Middle 22k lin
        m.res (dSrc, dM, 33e3);                              // R15
        m.cap (dM, dTB, 22e-9, T);                           // C11
        m.cap (dM, dMT, 22e-9, T);                           // C12
        const double rb = std::max (1.0, 1e6 * logTaper (masterPos));
        m.res (dTW, dW, std::max (1.0, 1e6 - rb));           // Master (topo -> cursor)
        m.res (dW, -1, rb);                                  // Master (cursor -> terra)
        m.res (dW, -1, 1e6);                                 // carga: C9 + R17 1M do inversor
        m.setNonlinear ({ dW }); m.reduce();
    }
    void buildE()
    {
        auto& m = E; m.clear();
        m.cond (eSrc, -1, gSrc);
        m.cap (eSrc, eG1, 22e-9, T);           // C9
        m.res (eG1, eA, 1e6);                  // R17
        m.res (eG2, eA, 1e6);                  // R19
        m.cap (eG2, eB, 0.1e-6, T);            // C15
        m.res (eK, eA, 470.0);                 // R16
        m.res (eA, eB, 10e3);                  // R20
        m.res (eB, -1, 4.7e3);                 // R23
        const double pr = std::clamp (knob[4] / 10.0, 0.0, 1.0);
        m.cap (eB, eP, 0.1e-6, T);             // C17
        m.res (eP, -1, std::max (10.0, 22e3 * (1.0 - pr)));   // Presence VR6 22k lin
        m.res (ePa, -1, 82e3);                 // R18 (ao X, fonte no rhs)
        m.res (ePb, -1, 100e3);                // R21
        m.cap (eG1, eK, ecc83Cgk, T); m.cap (eG1, ePa, ecc83Cgp, T); m.cap (ePa, eK, ecc83Cpk, T);
        m.cap (eG2, eK, ecc83Cgk, T); m.cap (eG2, ePb, ecc83Cgp, T); m.cap (ePb, eK, ecc83Cpk, T);
        m.cap (ePa, eGa, 22e-9, T);            // C14
        m.cap (ePb, eGb, 22e-9, T);            // C16
        m.res (eGa, -1, 220e3);                // R24 (ao bias, fonte no rhs)
        m.res (eGb, -1, 220e3);                // R25
        m.cap (eGa, eGb, 47e-12, T);           // C18
        m.res (eGa, eGA, 5.6e3 / 2.0);         // R31 || R33 (duas EL34 por lado)
        m.res (eGb, eGB, 5.6e3 / 2.0);         // R32 || R34
        m.cap (eGA, -1, 2.0 * 15e-12, T);      // EL34: capacitância de entrada (~15 pF cada)
        m.cap (eGB, -1, 2.0 * 15e-12, T);
        // Transformador de saída: primário 1,7 kΩ placa-placa com carga de 16 Ω (resistiva, como
        // numa load box), modo comum preso ao B+, indutância de magnetização 20 H
        m.res (ePA, ePB, Raa);
        m.G[ePA][ePA] += gCm * 0.5; m.G[ePA][ePB] += gCm * 0.5;
        m.G[ePB][ePA] += gCm * 0.5; m.G[ePB][ePB] += gCm * 0.5;
        m.ind (ePA, ePB, 20.0, T);
        // capacitância do enrolamento primário (~300 pF por lado) + saída das EL34 (~2 x 8 pF)
        m.cap (ePA, -1, 316e-12, T);
        m.cap (ePB, -1, 316e-12, T);
        // Realimentação: R22 100k do secundário (16 Ω) ao nó B
        const double gf = 1.0 / 100e3;
        m.G[eB][eB] += gf;
        m.G[eB][ePB] -= gf * fbSign / turns;
        m.G[eB][ePA] += gf * fbSign / turns;
        m.setNonlinear ({ eG1, eG2, eK, ePa, ePb, eGA, eGB, ePA, ePB });
        m.stepLimit[ePa] = m.stepLimit[ePb] = 100.0; m.stepLimit[ePA] = m.stepLimit[ePB] = 400.0;
        m.reduce();
    }

    void build()
    {
        gainPos = std::clamp (knob[0] / 10.0, 0.0, 1.0);
        masterPos = std::clamp (knob[5] / 10.0, 0.0, 1.0);
        buildA(); buildB(); buildC(); buildD(); buildE();
    }

    // Troca só as matrizes que dependem dos knobs, preservando o estado dos capacitores
    void applyKnobs()
    {
        knobsDirty = false;
        auto keep = [] (auto& m, auto&& rebuild)
        {
            auto old = m;
            rebuild();
            std::memcpy (m.v, old.v, sizeof (m.v));
            std::memcpy (m.vPrev, old.vPrev, sizeof (m.vPrev));
            for (int i = 0; i < m.nCaps; ++i) m.caps[(size_t) i].j = old.caps[(size_t) i].j * (m.caps[(size_t) i].g / old.caps[(size_t) i].g);
            for (int i = 0; i < m.nInds; ++i) m.inds[(size_t) i].j = old.inds[(size_t) i].j;
        };
        gainPos = std::clamp (knob[0] / 10.0, 0.0, 1.0);
        masterPos = std::clamp (knob[5] / 10.0, 0.0, 1.0);
        keep (B, [this] { buildB(); });
        keep (D, [this] { buildD(); });
        keep (E, [this] { buildE(); });
    }

    // Ponto de operação DC: resolve com capacitores abertos, ajusta os trilhos pelas correntes
    void operatingPoint()
    {
        vPlate = vPlateIdle; vScreen = vPlateIdle - chokeDrop;
        // Bias das EL34: ~35 mA por válvula em repouso (ajuste do trimpot RV1)
        {
            double lo = -80.0, hi = 0.0;
            for (int i = 0; i < 60; ++i)
            {
                const double mid = 0.5 * (lo + hi);
                if (el34 (mid, vPlate, vScreen).ip > 0.035) hi = mid; else lo = mid;
            }
            vBias = 0.5 * (lo + hi);
            idleMa = 1000.0 * el34 (vBias, vPlate, vScreen).ip;
        }
        // Trilhos: X = 460 V - R29 * (corrente do pré + inversor); V1 = X - R8 * (V1a + V1b)
        vRailX = 400.0; vRail1 = 330.0;
        for (int it = 0; it < 30; ++it)
        {
            dcSolveAll();
            const double i1 = (vRail1 - A.v[aP]) / 100e3 + (vRail1 - B.v[bP]) / 100e3;
            const double iV2a = (vRailX - C.v[cP]) / 100e3;
            const double iV2b = C.v[cK2] / 100e3;
            const double iPI = (vRailX - E.v[ePa]) / 82e3 + (vRailX - E.v[ePb]) / 100e3;
            const double iX = i1 + iV2a + iV2b + iPI;
            const double nx = vScreen - 10e3 * iX;
            const double n1 = nx - 10e3 * i1;
            vRailX += 0.5 * (nx - vRailX);
            vRail1 += 0.5 * (n1 - vRail1);
        }
        dcSolveAll();
        cfDc = C.v[cK2];
        {
            const double ia = el34 (E.v[eGA], E.v[ePA], vScreen).ip, ib = el34 (E.v[eGB], E.v[ePB], vScreen).ip;
            const double i1 = (vRail1 - A.v[aP]) / 100e3 + (vRail1 - B.v[bP]) / 100e3;
            iPre = i1 + (vRailX - C.v[cP]) / 100e3 + C.v[cK2] / 100e3 + (vRailX - E.v[ePa]) / 82e3 + (vRailX - E.v[ePb]) / 100e3;
            vNoLoad = vPlateIdle + Rs * (2.0 * (ia + ib) * 1.08 + iPre);
            dropX = vScreen - vRailX; dropV1 = vScreen - vRail1;
            railLp = T / 0.5;
        }
        A.settleHistory(); B.settleHistory(); C.settleHistory(); D.settleHistory(); E.settleHistory();
        // aquece 50 ms em silêncio para assentar tudo
        const int n = (int) (0.05 / T);
        for (int i = 0; i < n; ++i) process (0.0);
    }

    // Solução DC (capacitores = circuito aberto) de todos os blocos com entrada zero
    void dcSolveAll()
    {
        auto dc = [] (auto& m, auto&& rhsFill, auto&& dev)
        {
            auto saved = m;
            // remove os companheiros dos capacitores (abre) e curto nos indutores (condutância alta)
            for (int i = 0; i < m.nCaps; ++i) { const auto& c = m.caps[(size_t) i]; m.cond (c.a, c.b, -c.g); }
            for (int i = 0; i < m.nInds; ++i) { const auto& l = m.inds[(size_t) i]; m.cond (l.a, l.b, -l.g + 1e3); }
            // nós ligados só por capacitores ficam soltos: pequena condutância ao terra
            for (int r = 0; r < (int) (sizeof (m.v) / sizeof (m.v[0])); ++r) m.G[r][r] += 1e-12;
            m.reduce();
            std::memset (m.rhs, 0, sizeof (m.rhs));
            std::memcpy (m.vPrev, m.v, sizeof (m.v));
            rhsFill (m.rhs);
            newton (m, dev, 200);
            std::memcpy (saved.v, m.v, sizeof (m.v));
            std::memcpy (&m, &saved, sizeof (m));
        };
        dc (A, [&] (auto& r) { r[aP] += vRail1 / 100e3; },
            [&] (auto& J, auto& F, const auto& v) { stampTriode (J, F, v, aG, aK, aP, kGrid12AX7); });
        dc (B, [&] (auto& r) { r[bP] += vRail1 / 100e3; },
            [&] (auto& J, auto& F, const auto& v) { stampTriode (J, F, v, bW, bK, bP, kGrid12AX7); });
        dc (C, [&] (auto& r) { r[cP] += vRailX / 100e3; },
            [&] (auto& J, auto& F, const auto& v)
            {
                stampTriode (J, F, v, cG, cK, cP, kGrid12AX7);
                const double vg = v[cP], vk = v[cK2];
                const auto t = ecc83 (vg - vk, vRailX - vk);
                F[cK2] -= t.ip; J[cK2][cP] -= t.gm; J[cK2][cK2] += t.gm + t.gp;
            });
        dc (D, [&] (auto&) {}, [&] (auto&, auto&, const auto&) {});
        dc (E, [&] (auto& r)
            {
                r[ePa] += vRailX / 82e3; r[ePb] += vRailX / 100e3;
                r[eGa] += vBias / 220e3; r[eGb] += vBias / 220e3;
                r[ePA] += gCm * vPlate; r[ePB] += gCm * vPlate;
            },
            [&] (auto& J, auto& F, const auto& v)
            {
                stampTriode (J, F, v, eG1, eK, ePa, kGrid12AX7);
                stampTriode (J, F, v, eG2, eK, ePb, kGrid12AX7);
                for (int side = 0; side < 2; ++side)
                {
                    const int g = side == 0 ? eGA : eGB, p = side == 0 ? ePA : ePB;
                    const auto t = el34 (v[g], v[p], vScreen);
                    F[p] += 2.0 * t.ip; J[p][g] += 2.0 * t.gm; J[p][p] += 2.0 * t.gp;
                }
            });
    }
};
} // namespace vlv::sch

#include <juce_dsp/juce_dsp.h>
namespace vlv
{
// Motor "Schematic" no plugin: sobreamostragem 2x (só abaixo de 88 kHz) + simulação do 2203.
// Entrada: 0 dBFS = 1,5 V de pico no jack (nível de guitarra típico numa interface).
// Saída: tensão no alto-falante, normalizada para o mesmo volume do motor de circuito.
class SchematicAmp
{
public:
    static constexpr double inputVolts = 1.5;

    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        osFactor = fs < 88000.0 ? 1 : 0;           // 2x em 44,1/48 kHz; 1x em 88,2 kHz ou mais
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (1, (size_t) osFactor,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        oversampling->initProcessing ((size_t) juce::jmax (1, maxBlock));
        amp.prepare (fs * (1 << osFactor));
    }

    void reset()
    {
        amp.reset();
        if (oversampling) oversampling->reset();
    }

    void setKnobs (float gain, float bass, float mid, float treble, float presence, float master)
    {
        amp.setKnobs (gain, bass, mid, treble, presence, master);
    }

    void process (float* data, int numSamples)
    {
        float* chans[1] = { data };
        juce::dsp::AudioBlock<float> block (chans, 1, (size_t) numSamples);
        auto up = oversampling->processSamplesUp (block);
        float* x = up.getChannelPointer (0);
        const int n = (int) up.getNumSamples();
        const double outScale = std::pow (10.0, outTrimDb / 20.0) / 50.0;   // ~50 V no falante = 0 dBFS
        for (int i = 0; i < n; ++i)
            x[i] = (float) (amp.process ((double) x[i] * inputVolts) * outScale);
        oversampling->processSamplesDown (block);
    }

    // Nivelamento com o motor de circuito (mesmo sinal de teste, knobs no meio)
    static constexpr double outTrimDb = -12.1;

private:
    sch::Jcm800 amp;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    double fs = 48000;
    int osFactor = 1;
};
} // namespace vlv

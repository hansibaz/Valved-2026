#pragma once
// Volume igual para todos os amps e o mesmo comportamento do knob Master em todos.
//
// Medido no próprio plugin (loudness BS.1770, LUFS, gabinete ligado, knobs no meio, guitarra do
// sinal de teste) para cada amp, cada motor e o Master em 0,5 / 1 / 2 ... 10. Cada amp real
// reage diferente ao Master (o Rec quase não mudava acima de 4, o 51 Block tinha um degrau
// perto do 9); aqui a diferença é compensada para todos seguirem a mesma curva:
//   Master 5 = -18 LUFS, e cada vez que o Master dobra o volume sobe 6 dB (Master 10 = -12 LUFS).
// O timbre do Master (saturação da potência) continua o de cada amp; só o volume é igualado.
#include <cmath>
#include <algorithm>

namespace vlv
{
struct LevelMatch
{
    static constexpr float masterPoints[11] = { 0.5f, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    // [motor][amp][ponto]  motor 0 = Circuit, 1 = Neural
    static constexpr float measured[2][4][11] = {
        { { -31.60f, -25.66f, -19.98f, -16.93f, -14.94f, -13.51f, -12.42f, -11.54f, -10.82f, -10.22f,  -9.70f },   // Clean 120
          { -36.76f, -29.81f, -22.01f, -17.06f, -13.73f, -11.63f, -10.35f,  -9.54f,  -8.98f,  -8.55f,  -8.21f },   // Brit80
          { -49.78f, -42.71f, -34.48f, -28.62f, -23.71f, -19.34f, -15.42f, -12.02f,  -9.30f,  -7.35f,  -6.16f },   // 51 Block
          { -34.68f, -28.66f, -23.12f, -20.58f, -19.37f, -18.82f, -18.58f, -18.45f, -18.38f, -18.34f, -18.32f } }, // Rec
        { { -19.56f, -18.96f, -17.76f, -16.56f, -15.36f, -14.16f, -12.96f, -11.76f, -10.56f,  -9.36f,  -8.16f },
          { -28.01f, -25.54f, -20.91f, -16.62f, -13.40f, -11.40f, -10.21f,  -9.48f,  -9.06f,  -8.84f,  -8.74f },
          { -39.31f, -37.24f, -32.57f, -24.88f, -18.01f, -16.32f, -16.06f, -15.77f, -14.72f, -11.35f,  -5.48f },
          { -28.31f, -26.90f, -24.01f, -21.18f, -18.71f, -16.77f, -15.30f, -14.19f, -13.32f, -12.63f, -12.09f } } };

    static constexpr float targetLufs = -18.0f;

    // Ganho (linear) a aplicar depois do amp
    static float gain (int engine, int amp, float master)
    {
        engine = std::clamp (engine, 0, 1); amp = std::clamp (amp, 0, 3);
        const float m = std::clamp (master, 0.0f, 10.0f);
        const float* t = measured[engine][amp];
        float meas;
        if (m <= masterPoints[0]) meas = t[0];
        else
        {
            int i = 0;
            while (i < 9 && masterPoints[i + 1] < m) ++i;
            const float u = (m - masterPoints[i]) / (masterPoints[i + 1] - masterPoints[i]);
            meas = t[i] + u * (t[i + 1] - t[i]);
        }
        // abaixo de 0,5 o amp já fica bem baixo: segue a mesma curva até o silêncio
        const float want = targetLufs + 20.0f * std::log10 (std::max (m, 0.001f) / 5.0f);
        const float wantAtMeas = m < masterPoints[0] ? targetLufs + 20.0f * std::log10 (masterPoints[0] / 5.0f) : want;
        return std::pow (10.0f, (wantAtMeas - meas) / 20.0f) * (m < masterPoints[0] ? m / masterPoints[0] : 1.0f);
    }
};

// Limitador final: o mesmo teto para todos (-1 dBFS), nunca clipa.
// Ataque instantâneo (o ganho é calculado na própria amostra), retorno suave em ~150 ms.
class OutputLimiter
{
public:
    void prepare (double fs) { rel = (float) std::exp (-1.0 / (0.15 * fs)); g = 1.0f; }
    static constexpr float ceiling = 0.891f;   // -1 dBFS
    void process (float* L, float* R, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float pk = std::max (std::abs (L[i]), R != nullptr ? std::abs (R[i]) : 0.0f);
            g = 1.0f - rel * (1.0f - g);                       // volta para 1 devagar
            if (pk * g > ceiling) g = ceiling / pk;            // segura o pico na hora
            L[i] *= g;
            if (R != nullptr) R[i] *= g;
        }
    }
    float getGain() const { return g; }
private:
    float g = 1.0f, rel = 0.9999f;
};
} // namespace vlv

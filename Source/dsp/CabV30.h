#pragma once
// Alto-falante Celestion Vintage 30 (12", 8 ohms, 60 W, 100 dB, Fs 75 Hz, 70-5000 Hz).
// A resposta em frequência abaixo é a curva oficial "8 ohm Frequency Response" do fabricante
// (dB SPL, 1 W / 1 m), digitalizada em passos de 1/24 de oitava de 20 Hz a 20 kHz.
// O IR é gerado em fase mínima (como um alto-falante real), com a mesma magnitude da curva.
#include <vector>
#include <cmath>
#include <complex>
#include <algorithm>

namespace vlv
{
namespace v30
{
constexpr float sensitivityDb = 100.0f;   // 1 W / 1 m

// Curva oficial (Hz, dB SPL)
inline float curveDb (double f)
{
    static const float table[][2] = {
        { 20.0f, 77.03f }, { 20.6f, 77.03f }, { 21.2f, 77.44f }, { 21.8f, 77.88f }, { 22.4f, 78.36f }, { 23.1f, 78.99f },
        { 23.8f, 79.61f }, { 24.5f, 80.47f }, { 25.2f, 81.19f }, { 25.9f, 81.57f }, { 26.7f, 81.78f }, { 27.5f, 81.98f },
        { 28.3f, 81.98f }, { 29.1f, 82.00f }, { 30.0f, 82.31f }, { 30.8f, 82.64f }, { 31.7f, 83.13f }, { 32.7f, 83.55f },
        { 33.6f, 83.96f }, { 34.6f, 84.29f }, { 35.6f, 84.51f }, { 36.7f, 84.69f }, { 37.8f, 84.95f }, { 38.9f, 85.27f },
        { 40.0f, 85.27f }, { 41.2f, 85.34f }, { 42.4f, 85.50f }, { 43.6f, 85.60f }, { 44.9f, 85.93f }, { 46.2f, 86.33f },
        { 47.6f, 86.77f }, { 49.0f, 87.64f }, { 50.4f, 88.25f }, { 51.9f, 88.56f }, { 53.4f, 88.90f }, { 55.0f, 89.48f },
        { 56.6f, 89.89f }, { 58.2f, 90.40f }, { 59.9f, 90.88f }, { 61.7f, 90.88f }, { 63.5f, 90.88f }, { 65.4f, 91.21f },
        { 67.3f, 91.21f }, { 69.2f, 91.52f }, { 71.3f, 91.54f }, { 73.4f, 91.87f }, { 75.5f, 91.87f }, { 77.7f, 92.02f },
        { 80.0f, 92.24f }, { 82.3f, 92.45f }, { 84.8f, 92.58f }, { 87.2f, 92.86f }, { 89.8f, 93.17f }, { 92.4f, 93.46f },
        { 95.1f, 94.09f }, { 97.9f, 94.28f }, { 100.8f, 94.51f }, { 103.7f, 94.84f }, { 106.8f, 94.84f }, { 109.9f, 94.84f },
        { 113.1f, 95.16f }, { 116.5f, 95.16f }, { 119.9f, 95.37f }, { 123.4f, 95.49f }, { 127.0f, 95.82f }, { 130.7f, 95.82f },
        { 134.5f, 96.06f }, { 138.5f, 96.27f }, { 142.5f, 96.56f }, { 146.7f, 96.86f }, { 151.0f, 97.14f }, { 155.4f, 97.19f },
        { 160.0f, 97.24f }, { 164.7f, 97.29f }, { 169.5f, 97.34f }, { 174.5f, 97.38f }, { 179.6f, 97.43f }, { 184.9f, 97.47f },
        { 190.3f, 97.80f }, { 195.8f, 97.83f }, { 201.6f, 98.13f }, { 207.5f, 98.34f }, { 213.6f, 98.46f }, { 219.8f, 98.79f },
        { 226.3f, 98.79f }, { 232.9f, 98.79f }, { 239.7f, 98.79f }, { 246.8f, 98.79f }, { 254.0f, 98.79f }, { 261.4f, 99.12f },
        { 269.1f, 99.12f }, { 277.0f, 99.12f }, { 285.1f, 98.79f }, { 293.4f, 98.59f }, { 302.0f, 98.22f }, { 310.9f, 98.01f },
        { 320.0f, 97.80f }, { 329.4f, 97.80f }, { 339.0f, 97.80f }, { 349.0f, 97.80f }, { 359.2f, 97.80f }, { 369.7f, 97.80f },
        { 380.5f, 97.80f }, { 391.7f, 97.98f }, { 403.2f, 98.13f }, { 415.0f, 98.35f }, { 427.1f, 98.79f }, { 439.7f, 99.12f },
        { 452.5f, 99.29f }, { 465.8f, 99.75f }, { 479.5f, 99.91f }, { 493.5f, 100.11f }, { 508.0f, 100.11f }, { 522.9f, 100.44f },
        { 538.2f, 100.44f }, { 553.9f, 100.44f }, { 570.2f, 100.44f }, { 586.9f, 100.44f }, { 604.1f, 100.44f }, { 621.8f, 100.72f },
        { 640.0f, 100.77f }, { 658.8f, 100.77f }, { 678.1f, 100.77f }, { 697.9f, 100.77f }, { 718.4f, 100.77f }, { 739.4f, 100.77f },
        { 761.1f, 100.77f }, { 783.4f, 100.44f }, { 806.3f, 100.44f }, { 830.0f, 100.11f }, { 854.3f, 100.44f }, { 879.3f, 100.44f },
        { 905.1f, 100.59f }, { 931.6f, 100.77f }, { 958.9f, 100.77f }, { 987.0f, 100.77f }, { 1015.9f, 100.77f }, { 1045.7f, 100.77f },
        { 1076.3f, 100.17f }, { 1107.9f, 99.78f }, { 1140.4f, 99.40f }, { 1173.8f, 99.24f }, { 1208.2f, 98.99f }, { 1243.6f, 98.69f },
        { 1280.0f, 98.27f }, { 1317.5f, 98.31f }, { 1356.1f, 98.78f }, { 1395.8f, 99.41f }, { 1436.8f, 100.49f }, { 1478.9f, 101.01f },
        { 1522.2f, 101.43f }, { 1566.8f, 101.28f }, { 1612.7f, 101.10f }, { 1660.0f, 101.10f }, { 1708.6f, 101.10f }, { 1758.7f, 101.10f },
        { 1810.2f, 101.10f }, { 1863.2f, 101.70f }, { 1917.8f, 102.32f }, { 1974.0f, 103.27f }, { 2031.9f, 103.90f }, { 2091.4f, 104.52f },
        { 2152.7f, 105.10f }, { 2215.8f, 105.44f }, { 2280.7f, 105.71f }, { 2347.5f, 105.71f }, { 2416.3f, 105.43f }, { 2487.1f, 104.35f },
        { 2560.0f, 103.42f }, { 2635.0f, 102.75f }, { 2712.2f, 102.42f }, { 2791.7f, 102.42f }, { 2873.5f, 102.82f }, { 2957.7f, 103.77f },
        { 3044.4f, 104.40f }, { 3133.6f, 105.02f }, { 3225.4f, 105.38f }, { 3319.9f, 105.05f }, { 3417.2f, 104.53f }, { 3517.3f, 104.24f },
        { 3620.4f, 103.84f }, { 3726.5f, 103.65f }, { 3835.7f, 103.41f }, { 3948.1f, 103.06f }, { 4063.7f, 102.43f }, { 4182.8f, 101.81f },
        { 4305.4f, 101.52f }, { 4431.5f, 101.43f }, { 4561.4f, 100.93f }, { 4695.1f, 99.95f }, { 4832.6f, 98.70f }, { 4974.2f, 97.30f },
        { 5120.0f, 95.53f }, { 5270.0f, 93.36f }, { 5424.5f, 90.86f }, { 5583.4f, 88.95f }, { 5747.0f, 87.30f }, { 5915.4f, 86.41f },
        { 6088.7f, 86.11f }, { 6267.2f, 85.43f }, { 6450.8f, 84.37f }, { 6639.8f, 82.90f }, { 6834.4f, 81.96f }, { 7034.6f, 81.34f },
        { 7240.8f, 80.85f }, { 7452.9f, 80.54f }, { 7671.3f, 80.13f }, { 7896.1f, 79.25f }, { 8127.5f, 78.32f }, { 8365.6f, 77.92f },
        { 8610.8f, 77.63f }, { 8863.1f, 77.72f }, { 9122.8f, 78.34f }, { 9390.1f, 80.04f }, { 9665.3f, 80.91f }, { 9948.5f, 80.58f },
        { 10240.0f, 79.29f }, { 10540.1f, 77.58f }, { 10848.9f, 75.21f }, { 11166.8f, 73.66f }, { 11494.0f, 73.12f }, { 11830.8f, 74.24f },
        { 12177.5f, 75.18f }, { 12534.3f, 75.01f }, { 12901.6f, 74.16f }, { 13279.6f, 73.93f }, { 13668.8f, 74.89f }, { 14069.3f, 75.28f },
        { 14481.5f, 75.60f }, { 14905.9f, 75.91f }, { 15342.7f, 76.43f }, { 15792.2f, 77.67f }, { 16255.0f, 78.31f }, { 16731.3f, 79.22f },
        { 17221.6f, 79.34f }, { 17726.2f, 78.28f }, { 18245.6f, 75.80f }, { 18780.2f, 73.63f }, { 19330.5f, 74.14f }, { 19897.0f, 74.89f },
    };
    const int n = (int) (sizeof (table) / sizeof (table[0]));
    if (f <= table[0][0])   // abaixo de 20 Hz: continua caindo 12 dB/oitava (abaixo da ressonância)
        return table[0][1] - 12.0f * (float) std::log2 (table[0][0] / std::max (f, 1.0));
    if (f >= table[n - 1][0]) return table[n - 1][1];
    const double lf = std::log (f);
    int i = 0;
    while (i + 1 < n && table[i + 1][0] < f) ++i;
    const double l0 = std::log ((double) table[i][0]), l1 = std::log ((double) table[i + 1][0]);
    const double t = (lf - l0) / (l1 - l0);
    return (float) (table[i][1] + t * (table[i + 1][1] - table[i][1]));
}

// Posição do microfone (só altera a curva quando sai da posição padrão: centro 0,25 / distância 0,1)
inline double smoothStepLog (double f, double f0, double f1)   // 0 em f0 .. 1 em f1 (escala log)
{
    double t = (std::log (f) - std::log (f0)) / (std::log (f1) - std::log (f0));
    t = std::clamp (t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

inline double micDb (double f, double micX, double micDist)
{
    double db = 0.0;
    db += -12.0 * micX * smoothStepLog (f, 1500.0, 7000.0);                       // borda do cone: menos agudos
    const double oct = std::log2 (f / 320.0);
    db += 2.5 * micX * std::exp (-oct * oct * 2.0);                                // borda: mais corpo
    db += 5.0 * (1.0 - micDist) * (1.0 - smoothStepLog (f, 120.0, 400.0));        // efeito de proximidade
    db += -4.0 * micDist * smoothStepLog (f, 3000.0, 10000.0);                     // longe: perde brilho
    return db;
}

inline void fft (std::vector<std::complex<double>>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2.0 * 3.14159265358979323846 / (double) len * (inverse ? 1.0 : -1.0);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0);
            for (size_t j = 0; j < len / 2; ++j)
            {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v; a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse) for (auto& x : a) x /= (double) n;
}

// IR de fase mínima com ganho absoluto: 100 dB SPL (sensibilidade) = ganho 1,0
inline std::vector<float> makeIR (double fs, int length, double micX, double micDist)
{
    size_t N = 1;
    while (N < (size_t) length * 4) N <<= 1;
    std::vector<std::complex<double>> spec (N);
    const double refX = 0.25, refD = 0.1;
    for (size_t k = 0; k <= N / 2; ++k)
    {
        const double f = std::max (1.0, (double) k * fs / (double) N);
        double db = curveDb (f) - sensitivityDb + micDb (f, micX, micDist) - micDb (f, refX, refD);
        if (f > 20000.0) db -= 24.0 * std::log2 (f / 20000.0);    // acima do gráfico: cai 24 dB/oitava
        const double lnMag = db * std::log (10.0) / 20.0;
        spec[k] = lnMag;
        if (k > 0 && k < N / 2) spec[N - k] = lnMag;
    }
    // cepstro real -> fase mínima
    fft (spec, true);
    for (size_t i = 1; i < N / 2; ++i) { spec[i] *= 2.0; spec[N - i] = 0.0; }
    fft (spec, false);
    for (auto& x : spec) x = std::exp (x);
    fft (spec, true);
    std::vector<float> ir ((size_t) length);
    for (int i = 0; i < length; ++i) ir[(size_t) i] = (float) spec[(size_t) i].real();
    return ir;
}
} // namespace v30
} // namespace vlv

#pragma once
#include <cmath>
#include <algorithm>

namespace vlv
{
constexpr double kPi = 3.14159265358979323846;

// Filtro biquad (fórmulas RBJ). Mono, processa amostra a amostra.
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }

    inline float process (float xf) noexcept
    {
        const double x = xf;
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }

    void setRaw (double B0, double B1, double B2, double A0, double A1, double A2)
    {
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }

    static double clampF (double fs, double f) { return std::clamp (f, 10.0, fs * 0.49); }

    void lowpass (double fs, double f, double q)
    {
        const double w = 2 * kPi * clampF (fs, f) / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        setRaw ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }

    void highpass (double fs, double f, double q)
    {
        const double w = 2 * kPi * clampF (fs, f) / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        setRaw ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }

    void peak (double fs, double f, double q, double db)
    {
        const double A = std::pow (10.0, db / 40.0);
        const double w = 2 * kPi * clampF (fs, f) / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        setRaw (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }

    void lowShelf (double fs, double f, double db)
    {
        const double A = std::pow (10.0, db / 40.0);
        const double w = 2 * kPi * clampF (fs, f) / fs, c = std::cos (w), s = std::sin (w);
        const double al = s / 2 * std::sqrt (2.0), sa = 2 * std::sqrt (A) * al;
        setRaw (A * ((A + 1) - (A - 1) * c + sa),
                2 * A * ((A - 1) - (A + 1) * c),
                A * ((A + 1) - (A - 1) * c - sa),
                (A + 1) + (A - 1) * c + sa,
                -2 * ((A - 1) + (A + 1) * c),
                (A + 1) + (A - 1) * c - sa);
    }

    void highShelf (double fs, double f, double db)
    {
        const double A = std::pow (10.0, db / 40.0);
        const double w = 2 * kPi * clampF (fs, f) / fs, c = std::cos (w), s = std::sin (w);
        const double al = s / 2 * std::sqrt (2.0), sa = 2 * std::sqrt (A) * al;
        setRaw (A * ((A + 1) + (A - 1) * c + sa),
                -2 * A * ((A - 1) + (A + 1) * c),
                A * ((A + 1) + (A - 1) * c - sa),
                (A + 1) - (A - 1) * c + sa,
                2 * ((A - 1) - (A + 1) * c),
                (A + 1) - (A - 1) * c - sa);
    }
};

// Bloqueador de DC de 1 polo
struct DCBlocker
{
    double r = 0.995, x1 = 0, y1 = 0;
    void prepare (double fs, double cutoff = 15.0) { r = std::exp (-2 * kPi * cutoff / fs); reset(); }
    void reset() { x1 = y1 = 0; }
    inline float process (float x) noexcept
    {
        const double y = x - x1 + r * y1;
        x1 = x; y1 = y;
        return (float) y;
    }
};

inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
} // namespace vlv

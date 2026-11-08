#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Biquad.h"

namespace vlv
{
// Filtro de 1 polo (passa-baixa; passa-alta = x - passa-baixa)
struct OnePole
{
    float a = 0.0f, z = 0.0f;
    void setCutoff (double fs, double f) { a = (float) std::exp (-2.0 * kPi * juce::jlimit (5.0, fs * 0.45, f) / fs); }
    void reset() { z = 0.0f; }
    inline float lp (float x) noexcept { z = x + a * (z - x); return z; }
    inline float hp (float x) noexcept { return x - lp (x); }
};

enum PedalType { PedalGreen = 0, PedalYellow, PedalGold, PedalBlue, NumPedals };
} // namespace vlv

#include "PedalCircuits.h"

namespace vlv
{
// Os quatro overdrives (modelos de circuito em PedalCircuits.h):
//  Green = Ibanez TS808 / TS9, Yellow = Boss SD-1, Gold = Klon Centaur, Blue = King of Tone
class Overdrive
{
public:
    explicit Overdrive (PedalType t = PedalGreen) : type (t)
    {
        switch (type)
        {
            case PedalGreen:  impl = std::make_unique<pedal::TubeScreamer>(); break;
            case PedalYellow: impl = std::make_unique<pedal::SD1>(); break;
            case PedalGold:   impl = std::make_unique<pedal::Klon>(); break;
            case PedalBlue:
            default:          impl = std::make_unique<pedal::KingOfTone>(); break;
        }
    }
    Overdrive (Overdrive&&) noexcept = default;

    void prepare (double sampleRate, int maxBlock) { impl->prepare (sampleRate, maxBlock); }
    void setVariant (int v) { variant = v; }
    void setParams (bool on, float drive, float tone, float level)
    {
        impl->setEnabled (on);
        switch (type)
        {
            case PedalGreen:  static_cast<pedal::TubeScreamer*> (impl.get())->setParams (drive, tone, level, variant == 1); break;
            case PedalYellow: static_cast<pedal::SD1*> (impl.get())->setParams (drive, tone, level); break;
            case PedalGold:   static_cast<pedal::Klon*> (impl.get())->setParams (drive, tone, level); break;
            case PedalBlue:
            default:          static_cast<pedal::KingOfTone*> (impl.get())->setParams (drive, tone, level); break;
        }
    }
    void process (float* data, int n) { impl->process (data, n); }

private:
    PedalType type;
    int variant = 0;
    std::unique_ptr<pedal::PedalBase> impl;
};

inline const char* pedalName (int t)
{
    static const char* names[] = { "GREEN DRIVE", "YELLOW DRIVE", "GOLD DRIVE", "BLUE DRIVE" };
    return names[juce::jlimit (0, 3, t)];
}
} // namespace vlv

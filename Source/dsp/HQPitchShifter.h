#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>
#include <signalsmith-stretch/signalsmith-stretch.h>   // MIT — Signalsmith Audio

namespace vlv
{
// Transpose polifônico de alta qualidade (vocoder de fase com travamento de fase entre harmônicos).
// Não tem o "tremolo" do método de duas cabeças: as amplitudes ficam estáveis em acordes.
// Precisa de uma pequena latência, que é informada à DAW para compensação automática.
class HQPitchShifter
{
public:
    enum Quality { Live = 0, Studio };

    void prepare (double sampleRate, int maxBlock, Quality q = Studio)
    {
        fs = sampleRate;
        quality = q;
        // Janela longa o bastante para as notas graves de afinações baixas (Mi grave = 82 Hz)
        const double blockSec = q == Studio ? 0.10 : 0.06;
        const double hopSec   = q == Studio ? 0.020 : 0.012;
        stretch.configure (1, (int) (fs * blockSec), (int) (fs * hopSec));
        latency = stretch.inputLatency() + stretch.outputLatency();
        outBuf.assign ((size_t) juce::jmax (1, maxBlock), 0.0f);
        stretch.reset();
        active = false;
    }

    void setSemitones (int s)
    {
        if (s != semis)
        {
            semis = s;
            stretch.setTransposeSemitones ((float) s);
        }
    }

    int getSemitones() const { return semis; }
    int getLatencySamples() const { return latency; }
    bool isActive() const { return semis != 0; }

    void process (float* data, int n)
    {
        if (semis == 0)
        {
            if (active) { stretch.reset(); active = false; }
            return;
        }
        active = true;
        if ((int) outBuf.size() < n) outBuf.resize ((size_t) n);
        float* in[1] = { data };
        float* out[1] = { outBuf.data() };
        stretch.process (in, n, out, n);
        std::copy (outBuf.begin(), outBuf.begin() + n, data);
    }

private:
    signalsmith::stretch::SignalsmithStretch<float> stretch;
    std::vector<float> outBuf;
    double fs = 48000;
    Quality quality = Studio;
    int semis = 0, latency = 0;
    bool active = false;
};
} // namespace vlv

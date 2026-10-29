#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include "Biquad.h"
#include "TubeAmp.h"
#include "Overdrive.h"

namespace vlv
{
// "Voicing" de cada amplificador: define filtros, número de estágios de ganho,
// assimetria das válvulas e EQ. Os nomes são próprios (sem marcas registradas).
struct AmpVoicing
{
    const char* name;
    float inputHpf;          // corte de graves antes da distorção (deixa o som "apertado")
    float preMidFreq, preMidDb, preMidQ; // ênfase de médios antes da distorção
    int   stages;            // número de estágios de ganho (válvulas de pré)
    float minGainDb, maxGainDb;
    float headroom;          // < 1 = mais limpo (mais headroom)
    float bias;              // assimetria (harmônicos pares)
    float interStageLpf;     // filtro entre estágios (tira o "fizz")
    float bassFreq, midFreq, midQ, trebleFreq;
    float toneRangeDb;       // quanto cada knob de EQ corta/realça
    float midScoopDb;        // médio "cavado" característico
    float bassBaseDb;        // peso extra de graves
    float presenceFreq;
    float powerDrive;        // saturação do power amp
    float outTrimDb;         // compensação de volume entre modelos
    float resoFreq, resoQ, resoMaxDb; // knob Resonance (0 dB = o amp não tem esse controle)
};

// 0: Clean 120 (transistor, limpo cristalino com chorus estéreo)
// 1: Brit 410  (crunch / drive britânico, com Resonance)
// 2: 51 Block  (high gain agressivo, com Resonance)
// 3: Rec       (high gain pesado, com chave Vintage/Modern)
inline const std::array<AmpVoicing, 4>& getVoicings()
{
    static const std::array<AmpVoicing, 4> v {{
        { "Clean 120", 40.0f, 1000.0f, 0.0f, 0.7f, 1,  0.0f, 26.0f, 0.35f, 0.02f, 14000.0f,
          120.0f, 700.0f, 0.7f, 3500.0f, 12.0f,  0.0f, 0.0f, 4500.0f, 0.5f,  -1.0f,   0.0f, 1.0f,  0.0f },
        { "Brit 410",  75.0f,  850.0f, 5.0f, 0.8f, 3,  6.0f, 52.0f, 1.0f,  0.20f, 7500.0f,
          100.0f, 650.0f, 0.8f, 3000.0f, 10.0f, -1.5f, 0.0f, 3500.0f, 1.2f, -11.0f, 95.0f, 0.9f, 10.0f },
        { "51 Block",  110.0f, 750.0f, 7.0f, 0.9f, 4, 16.0f, 62.0f, 1.0f,  0.25f, 6500.0f,
          110.0f, 600.0f, 0.8f, 2800.0f, 10.0f, -4.0f, 1.0f, 3200.0f, 1.4f, -12.0f, 85.0f, 1.1f, 12.0f },
        { "Rec",       70.0f,  500.0f, 4.0f, 0.7f, 4, 16.0f, 62.0f, 1.0f,  0.15f, 6000.0f,
           90.0f, 500.0f, 0.7f, 3200.0f, 10.0f, -6.0f, 3.0f, 3500.0f, 1.6f, -12.0f,   0.0f, 1.0f,  0.0f },
    }};
    return v;
}

class AmpModel
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        oversampling->initProcessing ((size_t) juce::jmax (1, maxBlock));
        osFs = fs * 4.0;
        for (auto& d : dc) d.prepare (osFs, 12.0);
        // Circuitos valvulados (um por amp/modo, prontos para troca instantânea)
        const tube::CircuitSpec specs[NumCircuits] = { tube::brit80s(), tube::britModern(), tube::block5150(),
                                                       tube::recRectifier (false), tube::recRectifier (true) };
        for (int i = 0; i < NumCircuits; ++i)
        {
            auto sp = specs[i];
            sp.outTrimDb = circuitTrimDb[i];
            circuits[i].prepare (osFs, fs);
            circuits[i].configure (sp);
        }
        dirty = true;
        reset();
    }

    void reset()
    {
        inHpf.reset(); preMid.reset();
        for (auto& f : lpf) f.reset();
        for (auto& d : dc) d.reset();
        bass.reset(); mid.reset(); treble.reset(); presence.reset(); reso.reset();
        for (auto& c : circuits) c.reset();
        if (oversampling) oversampling->reset();
    }

    struct Settings
    {
        int model = 1;
        float gain = 5, bass = 5, mid = 5, treble = 5, presence = 5, master = 5, resonance = 5;
        bool recModern = true, britModern = false;
        bool operator!= (const Settings& o) const
        {
            return model != o.model || gain != o.gain || bass != o.bass || mid != o.mid || treble != o.treble // NOLINT
                || presence != o.presence || master != o.master || resonance != o.resonance || recModern != o.recModern
                || britModern != o.britModern;
        }
    };

    void setParameters (const Settings& s)
    {
        Settings n = s;
        n.model = juce::jlimit (0, 3, n.model);
        const bool voicingChange = n.model != set.model || (n.model == 3 && n.recModern != set.recModern)
                                || (n.model == 1 && n.britModern != set.britModern);
        if (n != set) { set = n; dirty = true; }
        if (voicingChange) reset();
    }

    void process (float* data, int numSamples)
    {
        if (dirty) updateCoefficients();

        float* chans[1] = { data };
        juce::dsp::AudioBlock<float> block (chans, 1, (size_t) numSamples);
        auto up = oversampling->processSamplesUp (block);
        float* u = up.getChannelPointer (0);
        const int nUp = (int) up.getNumSamples();

        // Amps valvulados: circuito completo no domínio sobreamostrado
        if (set.model != 0)
        {
            circuits[currentCircuit()].processOS (u, nUp);
            oversampling->processSamplesDown (block);
            return;
        }
        const auto& v = cur;
        const float h = v.headroom, inv = 1.0f / h;

        for (int i = 0; i < nUp; ++i)
        {
            float x = inHpf.process (u[i]);
            x = preMid.process (x);
            for (int s = 0; s < v.stages; ++s)
            {
                x *= stageGain;
                x = (std::tanh (h * x + v.bias) - tanhBias) * inv;
                x = dc[(size_t) s].process (x);
                x = lpf[(size_t) s].process (x);
            }
            u[i] = x;
        }
        oversampling->processSamplesDown (block);

        const float pd = v.powerDrive;
        for (int i = 0; i < numSamples; ++i)
        {
            float x = data[i];
            x = bass.process (x);
            x = mid.process (x);
            x = treble.process (x);
            x = presence.process (x);
            x = reso.process (x);   // Resonance: realce de graves no power amp
            x = std::tanh (x * masterGain * pd) / pd;
            data[i] = x * outTrim;
        }
    }

private:
public:
    enum { CircBrit80 = 0, CircBritModern, CircBlock, CircRecVintage, CircRecModern, NumCircuits };
    // Nivelamento de volume de cada circuito (medido com todos os knobs no meio)
    static constexpr float circuitTrimDb[NumCircuits] = { -14.2f, -13.5f, -12.5f, -13.35f, -15.6f };
    static float cleanTrimDb() { return -4.6f; }

private:
    int currentCircuit() const
    {
        switch (set.model)
        {
            case 1:  return set.britModern ? CircBritModern : CircBrit80;
            case 2:  return CircBlock;
            default: return set.recModern ? CircRecModern : CircRecVintage;
        }
    }

    void updateCoefficients()
    {
        dirty = false;
        if (set.model != 0)
        {
            circuits[currentCircuit()].setKnobs (set.gain, set.bass, set.mid, set.treble, set.presence, set.master, set.resonance);
            return;
        }
        cur = getVoicings()[(size_t) set.model];
        auto& v = cur;

        float gainOffsetDb = 0.0f, presenceExtraDb = 0.0f;
        if (set.model == 3)
        {
            if (set.recModern)
            {
                // Modern: mais ganho, médios cavados, graves soltos e power amp mais aberto
                gainOffsetDb = 4.0f; v.midScoopDb = -8.0f; v.bassBaseDb = 4.5f;
                v.interStageLpf = 6800.0f; v.powerDrive = 1.9f; presenceExtraDb = 1.5f;
                v.inputHpf = 65.0f; v.outTrimDb = -9.0f;
            }
            else
            {
                // Vintage: menos ganho, mais médios, mais apertado e suave nos agudos
                gainOffsetDb = -4.0f; v.midScoopDb = -1.5f; v.bassBaseDb = 0.5f;
                v.interStageLpf = 5200.0f; v.powerDrive = 1.2f; v.preMidDb = 6.0f;
                v.inputHpf = 95.0f; v.outTrimDb = -11.0f;
            }
        }

        inHpf.highpass (osFs, v.inputHpf, 0.707);
        if (v.preMidDb != 0.0f) preMid.peak (osFs, v.preMidFreq, v.preMidQ, v.preMidDb); // NOLINT
        else preMid.setRaw (1, 0, 0, 1, 0, 0);
        for (auto& f : lpf) f.lowpass (osFs, v.interStageLpf, 0.707);

        const float totalDb = v.minGainDb + (v.maxGainDb - v.minGainDb) * (set.gain / 10.0f) + gainOffsetDb;
        stageGain = dbToGain (totalDb / (float) v.stages);
        tanhBias = std::tanh (v.bias);

        auto knobDb = [&v] (float k) { return (k - 5.0f) / 5.0f * v.toneRangeDb; };
        bass.lowShelf (fs, v.bassFreq, knobDb (set.bass) + v.bassBaseDb);
        mid.peak (fs, v.midFreq, v.midQ, knobDb (set.mid) + v.midScoopDb);
        treble.highShelf (fs, v.trebleFreq, knobDb (set.treble));
        presence.highShelf (fs, v.presenceFreq, (set.presence - 5.0f) / 5.0f * 6.0f + presenceExtraDb);

        if (v.resoMaxDb > 0.0f)
            reso.peak (fs, v.resoFreq, v.resoQ, v.resoMaxDb * (set.resonance / 10.0f));
        else
            reso.setRaw (1, 0, 0, 1, 0, 0);

        masterGain = dbToGain (-14.0f + 24.0f * (set.master / 10.0f));
        outTrim = dbToGain (v.outTrimDb + cleanTrimDb());
    }

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    double fs = 48000, osFs = 192000;
    Settings set;
    AmpVoicing cur = getVoicings()[1];
    bool dirty = true;
    float stageGain = 1, tanhBias = 0, masterGain = 1, outTrim = 1;

    Biquad inHpf, preMid, bass, mid, treble, presence, reso;
    tube::CircuitAmp circuits[NumCircuits];
    std::array<Biquad, 4> lpf;
    std::array<DCBlocker, 4> dc;
};

// Chorus Ensemble no estilo Boss CE-1 (o mesmo circuito do chorus do JC-120):
//  - BBD de 512 estágios (MN3002) com atraso base de ~2,5 ms
//  - LFO triangular; no CE-1 o knob Intensity aumenta profundidade E velocidade (~1 Hz a ~3 Hz,
//    ~1,75 Hz no meio)
//  - Caminho molhado escuro: filtros anti-aliasing/reconstrução do BBD (~8 kHz), queda de ~3 dB/oitava
//    a partir de 1 kHz e leve realce de médios-graves
//  - Saídas estéreo como no JC-120: um lado direto (seco), o outro só efeito; em mono, 50/50
class CE1Chorus
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        buf.assign (4096, 0.0f);
        w = 0; lfo = 0.25;
        preLp.lowpass (fs, 8000.0, 0.707);  postLp.lowpass (fs, 8000.0, 0.707);
        postLp2.lowpass (fs, 9000.0, 0.54);
        tilt.setCutoff (fs, 1000.0);
        lowMid.peak (fs, 220.0, 0.8, 1.5);
        preLp.reset(); postLp.reset(); postLp2.reset(); tilt.reset(); lowMid.reset();
    }

    // intensity: 0..1 (knob DEPTH)   rateTrim: 0..1 (knob RATE; 0,5 = igual ao CE-1)
    void setControls (float intensity, float rateTrim)
    {
        intens = juce::jlimit (0.0f, 1.0f, intensity);
        const double mult = std::pow (2.0, (juce::jlimit (0.0f, 1.0f, rateTrim) - 0.5) * 2.0);
        rateHz = 1.0 * std::pow (3.0, (double) intens) * mult;
        widthMs = 0.6 + 3.4 * (double) intens;
    }

    inline void process (float x, float& direct, float& effect) noexcept
    {
        // Entrada do BBD (filtro anti-aliasing)
        buf[(size_t) w] = preLp.process (x);
        lfo += rateHz / fs;
        if (lfo >= 1.0) lfo -= 1.0;
        const double tri = lfo < 0.5 ? 4.0 * lfo - 1.0 : 3.0 - 4.0 * lfo;      // -1..1 triangular
        const double delayMs = baseMs + 0.5 * widthMs * (tri + 1.0);
        const double d = delayMs * 0.001 * fs;
        const double pos = (double) w - d;
        const double fl = std::floor (pos);
        const float t = (float) (pos - fl);
        const int i = (int) fl;
        auto at = [this] (int k) { return buf[(size_t) (k & 4095)]; };
        const float ym1 = at (i - 1), y0 = at (i), y1 = at (i + 1), y2 = at (i + 2);
        const float c1 = 0.5f * (y1 - ym1), c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2, c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        float wet = ((c3 * t + c2) * t + c1) * t + y0;
        w = (w + 1) & 4095;

        // Reconstrução do BBD + caráter escuro do CE-1
        wet = postLp2.process (postLp.process (wet));
        wet = 0.5f * wet + 0.5f * tilt.lp (wet);          // ~ -3 dB/oitava acima de 1 kHz
        wet = lowMid.process (wet);
        wet = std::tanh (wet * 1.3f) / 1.3f;               // o CE-1 satura fácil
        direct = x;
        effect = wet * 1.12f;
    }

private:
    std::vector<float> buf;
    double fs = 48000, lfo = 0.25, rateHz = 1.73, widthMs = 2.3;
    const double baseMs = 2.5;
    float intens = 0.5f;
    int w = 0;
    Biquad preLp, postLp, postLp2, lowMid;
    OnePole tilt;
};
} // namespace vlv

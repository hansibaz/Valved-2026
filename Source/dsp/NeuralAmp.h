#pragma once
// Motor neural do Valved: toca as capturas dos amps reais (WaveNet condicionado pelos knobs).
// Os modelos foram treinados a 48 kHz; em outras taxas o sinal é convertido para 48 kHz e de volta.
#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>
#include "NeuralModels.h"

namespace vlv
{
namespace nn
{
// tanh rápido (aproximação racional, erro ~1e-7) — vetoriza bem
static inline float fastTanh (float x) noexcept
{
    x = std::min (7.90531110763549805f, std::max (-7.90531110763549805f, x));
    const float x2 = x * x;
    float p = -2.76076847742355e-16f;
    p = p * x2 + 2.00018790482477e-13f;
    p = p * x2 + -8.60467152213735e-11f;
    p = p * x2 + 5.12229709037114e-08f;
    p = p * x2 + 1.48572235717979e-05f;
    p = p * x2 + 6.37261928875436e-04f;
    p = p * x2 + 4.89352455891786e-03f;
    p = p * x;
    float q = 1.19825839466702e-06f;
    q = q * x2 + 1.18534705686654e-04f;
    q = q * x2 + 2.26843463243900e-03f;
    q = q * x2 + 4.89352518554385e-03f;
    return p / q;
}

// WaveNet em tempo real: 16 canais, kernel 3, dilatações 1..512, ativação tanh*sigmoid,
// condicionamento pelos knobs em todas as camadas.
class WaveNet
{
public:
    static constexpr int C = 16, Layers = 10, MaxChunk = 128, MaxK = 8;

    void load (const float* params, int numKnobs)
    {
        K = numKnobs;
        // layout igual ao do treino
        size_t o = 0;
        Win = params + o; o += C; bin = params + o; o += C;
        const float* Wc = params + o; o += (size_t) 3 * 2 * C * C * Layers;
        bc = params + o; o += (size_t) 2 * C * Layers;
        Ux = params + o; o += (size_t) 2 * C * Layers;
        Vk = params + o; o += (size_t) 2 * C * K * Layers;
        R = params + o; o += (size_t) C * C * Layers;
        br = params + o; o += (size_t) C * Layers;
        Wo = params + o; o += C; bo = params + o;

        // Conv transposta para [camada][tap][c][o] (o laço interno em o vetoriza)
        WT.assign ((size_t) Layers * 3 * C * 2 * C, 0.0f);
        for (int l = 0; l < Layers; ++l)
            for (int tap = 0; tap < 3; ++tap)
                for (int oo = 0; oo < 2 * C; ++oo)
                    for (int c = 0; c < C; ++c)
                        WT[(((size_t) l * 3 + tap) * C + c) * 2 * C + oo] = Wc[(size_t) l * 3 * 2 * C * C + (size_t) tap * 2 * C * C + (size_t) oo * C + c];
        // R transposta [camada][c][o]
        RT.assign ((size_t) Layers * C * C, 0.0f);
        for (int l = 0; l < Layers; ++l)
            for (int oo = 0; oo < C; ++oo)
                for (int c = 0; c < C; ++c)
                    RT[((size_t) l * C + c) * C + oo] = R[(size_t) l * C * C + (size_t) oo * C + c];

        for (int l = 0; l < Layers; ++l)
        {
            dil[l] = 1 << l;
            int sz = 1;
            while (sz < 2 * dil[l] + MaxChunk + 1) sz <<= 1;
            mask[l] = sz - 1;
            ring[l].assign ((size_t) sz * C, 0.0f);
        }
        ck.assign ((size_t) Layers * 2 * C, 0.0f);
        skip.assign ((size_t) MaxChunk * C, 0.0f);
        float zero[MaxK] = {};
        setKnobs (zero);
        reset();
    }

    // knobs já codificados (valor/10 - 0,5)
    void setKnobs (const float* k)
    {
        for (int l = 0; l < Layers; ++l)
            for (int oo = 0; oo < 2 * C; ++oo)
            {
                float v = bc[l * 2 * C + oo];
                for (int j = 0; j < K; ++j) v += Vk[((size_t) l * 2 * C + oo) * K + j] * k[j];
                ck[(size_t) l * 2 * C + oo] = v;
            }
    }

    void reset()
    {
        for (int l = 0; l < Layers; ++l) std::fill (ring[l].begin(), ring[l].end(), 0.0f);
        w = 0;
        // aquece com silêncio (estado igual ao do treino, que começa com zeros na entrada)
        float z[MaxChunk] = {}, y[MaxChunk];
        for (int i = 0; i < 2048 / MaxChunk + 1; ++i) processChunk (z, y, MaxChunk);
    }

    void process (const float* x, float* y, int n)
    {
        while (n > 0)
        {
            const int m = n < MaxChunk ? n : MaxChunk;
            processChunk (x, y, m);
            x += m; y += m; n -= m;
        }
    }

private:
    void processChunk (const float* x, float* y, int n)
    {
        // entrada da 1ª camada
        {
            float* r0 = ring[0].data();
            for (int t = 0; t < n; ++t)
            {
                float* h = r0 + (size_t) ((w + t) & mask[0]) * C;
                for (int c = 0; c < C; ++c) h[c] = Win[c] * x[t] + bin[c];
            }
        }
        std::fill (skip.begin(), skip.begin() + (size_t) n * C, 0.0f);

        for (int l = 0; l < Layers; ++l)
        {
            const int d = dil[l], mk = mask[l];
            const float* rin = ring[l].data();
            float* rout = l + 1 < Layers ? ring[l + 1].data() : nullptr;
            const int mko = l + 1 < Layers ? mask[l + 1] : 0;
            const float* W0 = WT.data() + (size_t) l * 3 * C * 2 * C;
            const float* ckl = ck.data() + (size_t) l * 2 * C;
            const float* uxl = Ux + l * 2 * C;
            const float* rt = RT.data() + (size_t) l * C * C;
            const float* brl = br + l * C;

            for (int t = 0; t < n; ++t)
            {
                alignas (32) float z[2 * C];
                const float xt = x[t];
                for (int oo = 0; oo < 2 * C; ++oo) z[oo] = ckl[oo] + uxl[oo] * xt;
                const float* h0 = rin + (size_t) ((w + t) & mk) * C;
                for (int tap = 0; tap < 3; ++tap)
                {
                    const float* hp = rin + (size_t) ((w + t - tap * d) & mk) * C;
                    const float* Wt = W0 + (size_t) tap * C * 2 * C;
                    for (int c = 0; c < C; ++c)
                    {
                        const float hv = hp[c];
                        const float* wr = Wt + (size_t) c * 2 * C;
                        for (int oo = 0; oo < 2 * C; ++oo) z[oo] += wr[oo] * hv;
                    }
                }
                // tanh nas 32 saídas de uma vez (metade de cima vira sigmoid: 0,5 + 0,5*tanh(x/2))
                alignas (32) float th[2 * C];
                for (int i = 0; i < 2 * C; ++i) th[i] = fastTanh (z[i] * (i < C ? 1.0f : 0.5f));
                alignas (32) float g[C];
                for (int c = 0; c < C; ++c) g[c] = th[c] * (0.5f + 0.5f * th[C + c]);
                float* sk = skip.data() + (size_t) t * C;
                for (int c = 0; c < C; ++c) sk[c] += g[c];
                if (rout != nullptr)
                {
                    alignas (32) float acc[C];
                    for (int oo = 0; oo < C; ++oo) acc[oo] = h0[oo] + brl[oo];
                    for (int c = 0; c < C; ++c)
                    {
                        const float gv = g[c];
                        const float* rr = rt + (size_t) c * C;
                        for (int oo = 0; oo < C; ++oo) acc[oo] += rr[oo] * gv;
                    }
                    float* ho = rout + (size_t) ((w + t) & mko) * C;
                    for (int oo = 0; oo < C; ++oo) ho[oo] = acc[oo];
                }
            }
        }
        for (int t = 0; t < n; ++t)
        {
            float v = bo[0];
            const float* sk = skip.data() + (size_t) t * C;
            for (int c = 0; c < C; ++c) v += Wo[c] * sk[c];
            y[t] = v;
        }
        w = (w + n) & 0xFFFFF;   // todos os buffers são potências de 2 menores que isso
    }

    int K = 6;
    const float *Win = nullptr, *bin = nullptr, *bc = nullptr, *Ux = nullptr, *Vk = nullptr,
                *R = nullptr, *br = nullptr, *Wo = nullptr, *bo = nullptr;
    std::vector<float> WT, RT, ck, skip;
    std::vector<float> ring[Layers];
    int dil[Layers] {}, mask[Layers] {};
    int w = 0;
};

// Conversor de taxa de amostragem em tempo real (sinc com janela de Kaiser, tabela de fases)
class Resampler
{
public:
    static constexpr int HW = 24, Phases = 256;

    void prepare (double inRate, double outRate)
    {
        step = inRate / outRate;
        const double fc = 0.91 * std::min (1.0, outRate / inRate);   // corte relativo ao Nyquist da entrada
        table.assign ((size_t) (Phases + 1) * 2 * HW, 0.0f);
        const double beta = 8.0;
        auto bessel0 = [] (double v) { double s = 1, t = 1; for (int k = 1; k < 30; ++k) { t *= (v / (2 * k)) * (v / (2 * k)); s += t; } return s; };
        const double i0b = bessel0 (beta);
        for (int p = 0; p <= Phases; ++p)
        {
            const double frac = (double) p / Phases;
            for (int k = 0; k < 2 * HW; ++k)
            {
                const double xx = (double) (k - HW + 1) - frac;   // distância até a posição de saída
                const double u = xx / (double) HW;
                double win = std::abs (u) >= 1.0 ? 0.0 : bessel0 (beta * std::sqrt (1.0 - u * u)) / i0b;
                const double a = juce_pi_ * fc * xx;
                const double sinc = std::abs (xx) < 1e-9 ? 1.0 : std::sin (a) / a;
                table[(size_t) p * 2 * HW + (size_t) k] = (float) (fc * sinc * win);
            }
        }
        buf.assign (8192, 0.0f);
        reset();
    }

    void reset()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        count = HW;          // começa com HW zeros (atraso fixo)
        pos = (double) (HW - 1);
    }

    // Consome n amostras de entrada e escreve até maxOut amostras de saída; devolve quantas saíram
    int process (const float* in, int n, float* out, int maxOut)
    {
        if (count + n > (int) buf.size()) buf.resize ((size_t) (count + n) * 2, 0.0f);   // não deve acontecer
        std::memcpy (buf.data() + count, in, sizeof (float) * (size_t) n);
        count += n;
        int produced = 0;
        while (produced < maxOut)
        {
            const int i0 = (int) pos;
            if (i0 + HW >= count) break;
            const double fr = pos - i0;
            const double fp = fr * Phases;
            const int ph = (int) fp;
            const float t = (float) (fp - ph);
            const float* ta = table.data() + (size_t) ph * 2 * HW;
            const float* tb = ta + 2 * HW;
            const float* s = buf.data() + (i0 - HW + 1);
            float acc = 0.0f;
            for (int k = 0; k < 2 * HW; ++k) acc += s[k] * (ta[k] + t * (tb[k] - ta[k]));
            out[produced++] = acc;
            pos += step;
        }
        // descarta o que não será mais usado
        const int drop = (int) pos - HW + 1;
        if (drop > 0)
        {
            std::memmove (buf.data(), buf.data() + drop, sizeof (float) * (size_t) (count - drop));
            count -= drop;
            pos -= drop;
        }
        return produced;
    }

private:
    static constexpr double juce_pi_ = 3.14159265358979323846;
    std::vector<float> table, buf;
    double step = 1.0, pos = 0.0;
    int count = 0;
};
} // namespace nn

// Os 4 amps neurais. Ordem igual à do plugin: 0 Clean 120, 1 Brit80, 2 51 Block, 3 Rec
class NeuralAmp
{
public:
    NeuralAmp()
    {
        nets[0].load (neuraldata::clean120, neuraldata::clean120K);
        nets[1].load (neuraldata::brit80, neuraldata::brit80K);
        nets[2].load (neuraldata::block51, neuraldata::block51K);
        nets[3].load (neuraldata::rec, neuraldata::recK);
    }

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        fs = sampleRate;
        resampling = std::abs (fs - 48000.0) > 1.0;
        up.prepare (fs, 48000.0);
        down.prepare (48000.0, fs);
        const int cap = 4096;
        x48.assign (cap, 0.0f); y48.assign (cap, 0.0f); tmp.assign (cap, 0.0f);
        fifo.assign (1 << 14, 0.0f);
        reset (true);
    }

    void reset (bool allModels = false)
    {
        if (allModels) for (auto& n : nets) n.reset();
        else nets[model].reset();
        up.reset(); down.reset();
        std::fill (fifo.begin(), fifo.end(), 0.0f);
        fifoR = 0;
        fifoW = resampling ? prefill() : 0;
        smoothed = false;
        env = 0.0f; inScale = 1.0f;
        outGain = targetGain;
    }

    // Atraso total (em amostras da DAW) para compensação automática
    int getLatencySamples() const
    {
        static const int modelDelay[4] = { neuraldata::clean120Delay, neuraldata::brit80Delay, neuraldata::block51Delay, neuraldata::recDelay };
        const double md = modelDelay[model] * fs / 48000.0;
        return (int) std::lround (md) + (resampling ? prefill() : 0);
    }

    // Knobs na escala 0..10, na ordem do plugin
    void setParameters (int newModel, float gain, float bass, float mid, float treble, float presence, float master, float resonance)
    {
        newModel = std::clamp (newModel, 0, 3);
        if (newModel != model)
        {
            model = newModel;
            nets[model].reset();
            smoothed = false;
        }
        float k[8] {};
        auto enc = [] (float v) { return std::clamp (v, 0.0f, 10.0f) / 10.0f - 0.5f; };
        float fade = 1.0f, post = 1.0f;
        switch (model)
        {
            case 0:  // JC-120: Volume, Treble, Middle, Bass (gravado com Volume 1..10)
                k[0] = enc (std::max (gain, 1.0f)); k[1] = enc (treble); k[2] = enc (mid); k[3] = enc (bass);
                fade = std::min (1.0f, gain);                                  // Volume 0 = silêncio
                post = std::pow (10.0f, (master - 5.0f) * 1.2f / 20.0f);       // Master: ±6 dB
                break;
            case 1:  // JCM800: PreAmp, Bass, Middle, Treble, Presence, Master
                k[0] = enc (gain); k[1] = enc (bass); k[2] = enc (mid); k[3] = enc (treble); k[4] = enc (presence); k[5] = enc (master);
                fade = std::min (1.0f, gain / 0.5f);
                break;
            case 2:  // 5150: Pre, Low, Mid, High, Presence, Depth (Resonance), Post
                k[0] = enc (gain); k[1] = enc (bass); k[2] = enc (mid); k[3] = enc (treble); k[4] = enc (presence); k[5] = enc (resonance); k[6] = enc (master);
                fade = std::min (1.0f, gain / 0.5f);
                break;
            default: // Mesa: Gain, Bass, Mid, Treble, Presence, Master
                k[0] = enc (gain); k[1] = enc (bass); k[2] = enc (mid); k[3] = enc (treble); k[4] = enc (presence); k[5] = enc (master);
                fade = std::min (1.0f, gain / 0.5f);
                break;
        }
        for (int i = 0; i < 8; ++i) kTarget[i] = k[i];
        userGain = std::clamp (gain, 0.0f, 10.0f);
        targetGain = std::pow (10.0f, trimDb[model] / 20.0f) * fade * post;
    }

    void process (float* data, int n)
    {
        while (n > 0)
        {
            const int m = std::min (n, 1024);
            processChunk (data, m);
            data += m; n -= m;
        }
    }

    // Nivelamento de volume entre os amps (dB), medido com o sinal de treino e knobs no meio
    static constexpr float trimDb[4] = { -4.1f, 6.4f, -0.7f, -4.6f };   // Clean 120 baixado 5,5 dB (estava bem mais alto que os outros)   // = nível do motor de circuito (RMS, sinal de treino)

private:
    int prefill() const { return nn::Resampler::HW + (int) std::ceil (nn::Resampler::HW * fs / 48000.0) + 4; }

    // ---- Margem de entrada ----
    // Os modelos só aprenderam os amps com sinais até ~0 dBFS (o nível da gravação). Um pedal
    // empurrando o amp passa disso. No amp real, o sinal mais forte antes do potenciômetro de
    // ganho equivale a girar o ganho para cima (a 1ª válvula ainda está linear). Então, quando o
    // pico passa da margem, o sinal é reduzido para dentro da faixa aprendida e a diferença é
    // somada no knob de ganho (curva log do pot). Se o ganho já está no 10, o resto fica no sinal.
    static constexpr float headroom = 0.8f;
    static float potLog (float knob) { return (std::pow (10.0f, knob / 5.0f) - 1.0f) / 99.0f; }   // fração do pot A
    static float potLogInv (float frac) { return 5.0f * std::log10 (99.0f * frac + 1.0f); }

    void processChunk (float* data, int n)
    {
        auto& net = nets[model];
        {
            float pk = 0.0f;
            for (int i = 0; i < n; ++i) pk = std::max (pk, std::abs (data[i]));
            const float rel = std::exp (-(float) n / (float) (1.5 * fs));      // solta em ~1,5 s
            env = pk > env ? pk : std::max (pk, env * rel);
            float gEff = userGain, scale = 1.0f;
            if (env > headroom && userGain > 0.0f)
            {
                const float t = potLog (userGain);
                const float t2 = std::min (1.0f, t * env / headroom);
                gEff = potLogInv (t2);
                scale = t / t2;                                                 // o que foi para o knob sai do sinal
            }
            float k[8];
            for (int i = 0; i < 8; ++i) k[i] = kTarget[i];
            k[0] = std::clamp (model == 0 ? std::max (gEff, 1.0f) : gEff, 0.0f, 10.0f) / 10.0f - 0.5f;
            if (! smoothed) { for (int i = 0; i < 8; ++i) knob[i] = k[i]; inScale = scale; smoothed = true; }
            else for (int i = 0; i < 8; ++i) knob[i] += 0.35f * (k[i] - knob[i]);
            net.setKnobs (knob);
            const float st = (scale - inScale) / (float) n;
            for (int i = 0; i < n; ++i) { inScale += st; data[i] *= inScale; }
            inScale = scale;
        }
        if (! resampling)
        {
            net.process (data, data, n);
        }
        else
        {
            const int m = up.process (data, n, x48.data(), (int) x48.size());
            net.process (x48.data(), y48.data(), m);
            const int k = down.process (y48.data(), m, tmp.data(), (int) tmp.size());
            const int fm = (int) fifo.size() - 1;
            for (int i = 0; i < k; ++i) { fifo[(size_t) (fifoW & fm)] = tmp[(size_t) i]; ++fifoW; }
            for (int i = 0; i < n; ++i)
            {
                data[i] = fifoR < fifoW ? fifo[(size_t) (fifoR & fm)] : 0.0f;
                if (fifoR < fifoW) ++fifoR;
            }
        }
        const float step = (targetGain - outGain) / (float) n;
        for (int i = 0; i < n; ++i) { outGain += step; data[i] *= outGain; }
        outGain = targetGain;
    }

    nn::WaveNet nets[4];
    nn::Resampler up, down;
    std::vector<float> x48, y48, tmp, fifo;
    long long fifoR = 0, fifoW = 0;
    double fs = 48000;
    bool resampling = false, smoothed = false;
    int model = 1;
    float knob[8] {}, kTarget[8] {};
    float userGain = 5.0f, env = 0.0f, inScale = 1.0f;
    float outGain = 1.0f, targetGain = 1.0f;
};
} // namespace vlv

// Ferramenta de teste: toca uma gravação de guitarra (DI, float32 mono 48 kHz, sem cabeçalho)
// por todos os presets de fábrica e grava um WAV de cada um. Serve para conferir se o build de
// cada sistema (Windows / macOS / Linux) soa igual e não gera NaN, estalo ou silêncio.
//   ValvedRender <entrada.f32> <pasta de saída>
#include "PluginProcessor.h"
#include "Presets.h"
#include <juce_audio_formats/juce_audio_formats.h>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3) { printf ("uso: ValvedRender entrada.f32 pasta\n"); return 2; }
    juce::MemoryBlock mb;
    if (! juce::File (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])).loadFileAsData (mb)) { printf ("não li a entrada\n"); return 2; }
    const float* x = (const float*) mb.getData();
    const int N = (int) (mb.getSize() / 4);
    const double fs = 48000.0; const int block = 256;
    auto outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
    outDir.createDirectory();
    int failures = 0;
    for (int preset = 0; preset < PresetManager::NumFactory; ++preset)
    {
        ValvedProcessor p;
        p.setPlayConfigDetails (2, 2, fs, block);
        p.prepareToPlay (fs, block);
        PresetManager (p).load (preset);
        juce::AudioBuffer<float> out (2, N), b (2, block);
        juce::MidiBuffer midi;
        // como numa DAW: deixa o plugin terminar de carregar (caixa/IR em segundo plano) antes de tocar
        for (int k = 0; k < 40; ++k)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
            b.clear(); p.processBlock (b, midi);
        }
        bool bad = false; double sum = 0; float peak = 0;
        for (int i0 = 0; i0 < N; i0 += block)
        {
            const int n = juce::jmin (block, N - i0);
            b.clear();
            for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, x + i0, n);
            p.processBlock (b, midi);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                {
                    const float v = b.getSample (c, i);
                    if (! std::isfinite (v)) bad = true;
                    out.setSample (c, i0 + i, v);
                    if (c == 0) { sum += (double) v * v; peak = juce::jmax (peak, std::abs (v)); }
                }
        }
        const double rmsDb = 10.0 * std::log10 (sum / N + 1e-20);
        const auto name = PresetManager::factoryNames()[preset];
        const bool silent = rmsDb < -70.0;
        if (bad || silent) ++failures;
        printf ("%02d %-18s rms %6.2f dB  pico %.3f  %s\n", preset + 1, name.toRawUTF8(), rmsDb, peak, bad ? "NaN!" : (silent ? "SILENCIO!" : "ok"));
        auto f = outDir.getChildFile (juce::String (preset + 1).paddedLeft ('0', 2) + " " + name + ".wav");
        f.deleteFile();
        juce::WavAudioFormat wav;
        if (auto os = std::unique_ptr<juce::OutputStream> (f.createOutputStream()))
            if (auto* w = wav.createWriterFor (os.get(), fs, 2, 24, {}, 0))
            {
                os.release();
                std::unique_ptr<juce::AudioFormatWriter> writer (w);
                writer->writeFromAudioSampleBuffer (out, 0, N);
            }
    }
    printf (failures == 0 ? "TUDO OK\n" : "FALHAS: %d\n", failures);
    return failures == 0 ? 0 : 1;
}

#pragma once
// Backing track: um arquivo de áudio (WAV, MP3, AIFF, FLAC, OGG) que toca junto com a guitarra.
// Ele sai na saída do Valved e vai junto para o app Valved Cam, para gravar o vídeo já com a base.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>

namespace vlv
{
class BackingTrack
{
public:
    BackingTrack() : thumbCache (2), thumbnail (512, formats, thumbCache)
    {
        formats.registerBasicFormats();
        readAhead.startThread (juce::Thread::Priority::normal);
    }

    ~BackingTrack()
    {
        transport.setSource (nullptr);
        readAhead.stopThread (2000);
    }

    // ---------- arquivo (thread da tela)
    juce::String wildcard() const { return formats.getWildcardForAllFormats(); }

    bool load (const juce::File& f)
    {
        auto* reader = formats.createReaderFor (f);
        if (reader == nullptr) return false;
        const double sr = reader->sampleRate;
        const double len = sr > 0 ? (double) reader->lengthInSamples / sr : 0.0;
        auto src = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
        src->setLooping (loop.load());
        transport.stop();
        transport.setSource (nullptr);
        source = std::move (src);
        transport.setSource (source.get(), 1 << 16, &readAhead, sr, 2);
        thumbnail.setSource (new juce::FileInputSource (f));
        {
            const juce::ScopedLock sl (fileLock);
            file = f;
        }
        lengthSec = len;
        return true;
    }

    void clear()
    {
        transport.stop();
        transport.setSource (nullptr);
        source.reset();
        thumbnail.clear();
        const juce::ScopedLock sl (fileLock);
        file = juce::File();
        lengthSec = 0;
    }

    juce::File getFile() const { const juce::ScopedLock sl (fileLock); return file; }
    bool hasFile() const { return source != nullptr; }
    double getLength() const { return lengthSec.load(); }
    juce::AudioThumbnail& getThumbnail() { return thumbnail; }

    // ---------- transporte
    void play()
    {
        if (source == nullptr) return;
        if (transport.getCurrentPosition() >= lengthSec.load() - 0.01) transport.setPosition (0);
        transport.start();
    }
    void pause() { transport.stop(); }
    void stop() { transport.stop(); transport.setPosition (0); }
    bool isPlaying() const { return transport.isPlaying(); }
    double getPosition() const { return transport.getCurrentPosition(); }
    void setPosition (double s) { transport.setPosition (juce::jlimit (0.0, juce::jmax (0.0, lengthSec.load()), s)); }

    void setLoop (bool l) { loop = l; if (source != nullptr) source->setLooping (l); }
    bool getLoop() const { return loop.load(); }

    // volume em dB (-60 = mudo) e se vai junto para o Valved Cam
    std::atomic<float> volumeDb { -6.0f };
    std::atomic<bool> sendToCam { true };

    // ---------- áudio
    void prepare (double sampleRate, int blockSize)
    {
        transport.prepareToPlay (blockSize, sampleRate);
        temp.setSize (2, juce::jmax (1, blockSize), false, true, false);
        gain.reset (sampleRate, 0.05);
        gain.setCurrentAndTargetValue (dbToGain (volumeDb.load()));
    }

    void release() { transport.releaseResources(); }

    /** Soma a base em L/R (R pode ser nulo = saída mono). Não faz nada se não estiver tocando. */
    void addTo (float* L, float* R, int n)
    {
        gain.setTargetValue (dbToGain (volumeDb.load()));
        if (! transport.isPlaying() || n <= 0) return;
        int done = 0;
        while (done < n)
        {
            const int k = juce::jmin (n - done, temp.getNumSamples());
            juce::AudioSourceChannelInfo info (&temp, 0, k);
            transport.getNextAudioBlock (info);
            const float* a = temp.getReadPointer (0);
            const float* b = temp.getReadPointer (1);
            for (int i = 0; i < k; ++i)
            {
                const float g = gain.getNextValue();
                if (R != nullptr) { L[done + i] += a[i] * g; R[done + i] += b[i] * g; }
                else L[done + i] += 0.5f * (a[i] + b[i]) * g;
            }
            done += k;
        }
        if (! loop.load() && transport.hasStreamFinished()) transport.stop();
    }

private:
    static float dbToGain (float db) { return db <= -59.5f ? 0.0f : juce::Decibels::decibelsToGain (db); }

    juce::AudioFormatManager formats;
    juce::TimeSliceThread readAhead { "Valved backing track" };
    juce::AudioTransportSource transport;
    std::unique_ptr<juce::AudioFormatReaderSource> source;
    juce::AudioThumbnailCache thumbCache;
    juce::AudioThumbnail thumbnail;
    juce::AudioBuffer<float> temp;
    juce::SmoothedValue<float> gain;
    juce::CriticalSection fileLock;
    juce::File file;
    std::atomic<double> lengthSec { 0 };
    std::atomic<bool> loop { true };

    JUCE_DECLARE_NON_COPYABLE (BackingTrack)
};
} // namespace vlv

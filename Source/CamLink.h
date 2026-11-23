#pragma once
// Valved Cam: manda o som que sai do Valved, em tempo real, para o app Valved Cam (Android).
// O app grava o vídeo da câmera do celular e junta com esse som no mesmo arquivo.
//
// Como se encontram: o app anuncia "VALVEDCAM|<nome>|<porta>" por UDP (porta 47801) a cada segundo,
// na rede Wi-Fi ou no cabo USB (ancoragem USB). O plugin escuta, conecta por TCP na porta anunciada
// e envia: cabeçalho "VLVA" + taxa de amostragem (int32) + canais (int16 = 2) + bits (int16 = 16),
// depois o áudio PCM 16 bits estéreo intercalado, sem parar, enquanto estiver conectado.
#include <juce_core/juce_core.h>
#include <atomic>
#include <vector>

namespace vlv
{
class CamLink : private juce::Thread
{
public:
    enum Status { Off = 0, Searching, Connected };
    static constexpr int beaconPort = 47801;

    CamLink() : juce::Thread ("Valved Cam link"), fifo (capacity)
    {
        bufL.assign ((size_t) capacity, 0.0f);
        bufR.assign ((size_t) capacity, 0.0f);
        startThread (juce::Thread::Priority::normal);
    }
    ~CamLink() override
    {
        signalThreadShouldExit();
        stopThread (3000);
    }

    // liga/desliga pelo botão CAM do plugin
    void setEnabled (bool e) { enabled = e; }
    bool isEnabled() const { return enabled.load(); }
    Status getStatus() const { return (Status) status.load(); }
    juce::String getDeviceName() const { const juce::ScopedLock sl (nameLock); return deviceName; }

    void setSampleRate (double sr) { if (sr > 0) sampleRate = (int) std::lround (sr); }

    // chamado pela thread de áudio no fim do processBlock (não bloqueia, não aloca)
    void push (const float* L, const float* R, int n)
    {
        if (status.load (std::memory_order_relaxed) != Connected || n <= 0) return;
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);   // se a rede atrasar e a fila encher, o resto é descartado
        auto copy = [&] (int start, int count, int off)
        {
            for (int i = 0; i < count; ++i)
            {
                bufL[(size_t) (start + i)] = L[off + i];
                bufR[(size_t) (start + i)] = R != nullptr ? R[off + i] : L[off + i];
            }
        };
        copy (s1, n1, 0);
        copy (s2, n2, n1);
        fifo.finishedWrite (n1 + n2);
    }

private:
    static constexpr int capacity = 48000 * 4;   // até ~4 s de folga na fila

    void run() override
    {
        juce::DatagramSocket udp (true);
        bool bound = false;
        std::vector<char> pcm;

        while (! threadShouldExit())
        {
            if (! enabled.load())
            {
                closeSocket();
                status = Off;
                wait (200);
                continue;
            }
            if (! bound)
            {
                udp.setEnablePortReuse (true);   // várias instâncias do plugin podem escutar juntas
                bound = udp.bindToPort (beaconPort);
            }
            if (! bound) { status = Searching; wait (1000); continue; }

            if (tcp == nullptr)
            {
                status = Searching;
                if (udp.waitUntilReady (true, 300) != 1) continue;
                char msg[256] = {};
                juce::String fromIp; int fromPort = 0;
                const int got = udp.read (msg, (int) sizeof (msg) - 1, false, fromIp, fromPort);
                if (got <= 0) continue;
                auto parts = juce::StringArray::fromTokens (juce::String::fromUTF8 (msg, got), "|", "");
                if (parts.size() < 3 || parts[0] != "VALVEDCAM") continue;
                const int port = parts[2].getIntValue();
                if (port <= 0 || fromIp.isEmpty()) continue;

                auto s = std::make_unique<juce::StreamingSocket>();
                if (! s->connect (fromIp, port, 1500)) continue;
                if (! sendHeader (*s)) continue;
                { const juce::ScopedLock sl (nameLock); deviceName = parts[1]; }
                fifo.reset();
                tcp = std::move (s);
                status = Connected;
                continue;
            }

            // conectado: esvazia a fila e manda pela rede
            const int ready = fifo.getNumReady();
            if (ready < 64) { wait (4); continue; }
            const int n = juce::jmin (ready, 4096);
            pcm.resize ((size_t) n * 4);
            int s1, n1, s2, n2;
            fifo.prepareToRead (n, s1, n1, s2, n2);
            int w = 0;
            auto conv = [&] (int start, int count)
            {
                for (int i = 0; i < count; ++i)
                    for (float v : { bufL[(size_t) (start + i)], bufR[(size_t) (start + i)] })
                    {
                        const auto q = (int16_t) juce::jlimit (-32767, 32767, (int) std::lround (v * 32767.0f));
                        pcm[(size_t) w++] = (char) (q & 0xff);
                        pcm[(size_t) w++] = (char) ((q >> 8) & 0xff);
                    }
            };
            conv (s1, n1);
            conv (s2, n2);
            fifo.finishedRead (n1 + n2);
            if (! writeAll (pcm.data(), w)) closeSocket();
        }
        closeSocket();
    }

    bool sendHeader (juce::StreamingSocket& s)
    {
        char h[12] = { 'V', 'L', 'V', 'A' };
        const int sr = sampleRate.load();
        for (int i = 0; i < 4; ++i) h[4 + i] = (char) ((sr >> (8 * i)) & 0xff);
        h[8] = 2; h[9] = 0;     // 2 canais
        h[10] = 16; h[11] = 0;  // 16 bits
        return s.write (h, 12) == 12;
    }

    bool writeAll (const char* d, int len)
    {
        int done = 0;
        while (done < len)
        {
            if (tcp == nullptr || threadShouldExit()) return false;
            if (tcp->waitUntilReady (false, 1000) != 1) return false;
            const int k = tcp->write (d + done, len - done);
            if (k <= 0) return false;
            done += k;
        }
        return true;
    }

    void closeSocket()
    {
        if (tcp != nullptr) { tcp->close(); tcp.reset(); }
        if (status.load() == Connected) status = Searching;
    }

    std::atomic<bool> enabled { false };
    std::atomic<int> status { Off };
    std::atomic<int> sampleRate { 48000 };
    juce::AbstractFifo fifo;
    std::vector<float> bufL, bufR;
    std::unique_ptr<juce::StreamingSocket> tcp;
    juce::CriticalSection nameLock;
    juce::String deviceName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CamLink)
};
} // namespace vlv

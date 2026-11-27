#pragma once
// Valved Cam: manda o som que sai do Valved, em tempo real, para o app Valved Cam (Android).
// O app grava o vídeo da câmera do celular e junta com esse som no mesmo arquivo.
//
// Como se encontram: o app anuncia "VALVEDCAM|<nome>|<porta>" por UDP (porta 47801) a cada segundo,
// na rede Wi-Fi ou no cabo USB (ancoragem USB). O plugin escuta, conecta por TCP na porta anunciada
// e envia: cabeçalho "VLVA" + taxa de amostragem (int32) + canais (int16 = 2) + bits (int16 = 16),
// depois o áudio PCM 16 bits estéreo intercalado, sem parar, enquanto estiver conectado.
//
// iPhone: o iOS não deixa o app anunciar por UDP (broadcast). Então, quando nenhum anúncio chega,
// o plugin procura sozinho: tenta a porta 47800 em todos os endereços da rede local (/24) e aceita
// quem responder "VALVEDCAM|<nome>|<porta>\n" logo ao conectar (o app de iPhone faz isso).
#include <juce_core/juce_core.h>
#include <atomic>
#include <vector>
#include <thread>

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
        juce::int64 lastBeacon = (juce::int64) juce::Time::getMillisecondCounterHiRes(), lastScan = 0;
        std::vector<char> pcm;

        while (! threadShouldExit())
        {
            if (! enabled.load())
            {
                closeSocket();
                status = Off;
                lastBeacon = (juce::int64) juce::Time::getMillisecondCounterHiRes();   // ao ligar, espera 1,5 s pelo anúncio (Android) antes de procurar (iPhone)
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
                if (udp.waitUntilReady (true, 300) != 1)
                {
                    // nenhum anúncio (iPhone não anuncia): procura o app na rede
                    const auto now = juce::Time::getMillisecondCounterHiRes();
                    if (now - (double) lastBeacon > 1500.0 && now - (double) lastScan > 1500.0)
                    {
                        juce::String name;
                        auto s = scanForApp (name);
                        lastScan = (juce::int64) juce::Time::getMillisecondCounterHiRes();
                        if (s != nullptr && enabled.load() && sendHeader (*s))
                        {
                            { const juce::ScopedLock sl (nameLock); deviceName = name; }
                            fifo.reset();
                            tcp = std::move (s);
                            status = Connected;
                        }
                    }
                    continue;
                }
                char msg[256] = {};
                juce::String fromIp; int fromPort = 0;
                const int got = udp.read (msg, (int) sizeof (msg) - 1, false, fromIp, fromPort);
                if (got <= 0) continue;
                auto parts = juce::StringArray::fromTokens (juce::String::fromUTF8 (msg, got), "|", "");
                if (parts.size() < 3 || parts[0] != "VALVEDCAM") continue;
                lastBeacon = (juce::int64) juce::Time::getMillisecondCounterHiRes();
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

    // Procura o app na rede local: tenta a porta 47800 em todos os vizinhos (/24) ao mesmo tempo
    // e fica com o primeiro que se apresentar como Valved Cam.
    std::unique_ptr<juce::StreamingSocket> scanForApp (juce::String& nameOut)
    {
        juce::StringArray hosts;
        for (auto& a : juce::IPAddress::getAllAddresses (false))
        {
            const int b0 = a.address[0], b1 = a.address[1], b2 = a.address[2], b3 = a.address[3];
            const bool priv = b0 == 10 || (b0 == 172 && b1 >= 16 && b1 <= 31) || (b0 == 192 && b1 == 168);
            if (! priv) continue;   // só redes locais
            for (int h = 1; h < 255; ++h)
                if (h != b3) hosts.addIfNotAlreadyThere (juce::String (b0) + "." + juce::String (b1) + "." + juce::String (b2) + "." + juce::String (h));
        }
       #ifdef VLV_CAMLINK_TEST_SCAN
        hosts.add ("127.0.0.1");   // só no teste automático
       #endif
        if (hosts.isEmpty()) return nullptr;
        // o último iPhone encontrado vai primeiro (reconecta na hora)
        {
            const juce::ScopedLock sl (nameLock);
            if (lastAppIp.isNotEmpty()) { hosts.removeString (lastAppIp); hosts.insert (0, lastAppIp); }
        }

        juce::CriticalSection foundLock;
        std::unique_ptr<juce::StreamingSocket> found;
        juce::String foundName, foundIp;
        std::atomic<int> next { 0 };
        auto worker = [&]
        {
            for (;;)
            {
                const int i = next++;
                if (i >= hosts.size() || threadShouldExit()) return;
                { const juce::ScopedLock sl (foundLock); if (found != nullptr) return; }
                auto s = std::make_unique<juce::StreamingSocket>();
                if (! s->connect (hosts[i], 47800, i == 0 ? 600 : 300)) continue;
                char hello[128] = {};
                int got = 0;
                while (got < (int) sizeof (hello) - 1 && s->waitUntilReady (true, 800) == 1)
                {
                    const int k = s->read (hello + got, 1, false);
                    if (k <= 0 || hello[got] == '\n') break;
                    ++got;
                }
                auto parts = juce::StringArray::fromTokens (juce::String::fromUTF8 (hello, got), "|", "");
                if (parts.size() < 2 || parts[0] != "VALVEDCAM") continue;
                const juce::ScopedLock sl (foundLock);
                if (found == nullptr) { found = std::move (s); foundName = parts[1]; foundIp = hosts[i]; }
            }
        };
        std::vector<std::unique_ptr<std::thread>> pool;
        for (int t = 0; t < 64; ++t) pool.push_back (std::make_unique<std::thread> (worker));
        for (auto& t : pool) t->join();
        nameOut = foundName;
        if (foundIp.isNotEmpty()) { const juce::ScopedLock sl (nameLock); lastAppIp = foundIp; }
        return std::move (found);
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
    juce::String deviceName, lastAppIp;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CamLink)
};
} // namespace vlv

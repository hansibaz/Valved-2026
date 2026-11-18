// Ferramenta de captura: hospeda um plugin VST2 (Windows), toca o sinal de treino por ele em
// várias posições de knobs e grava as saídas. Usada só no GitHub Actions (não vai no Valved).
//   capture list   <plugin.dll>
//   capture render <plugin.dll> <di.f32> <settings.txt> <saida_dir>
// settings.txt: uma linha por take:  nome idx=valor idx=valor ...   (valores normalizados 0..1)
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <cmath>
#include <algorithm>

struct AEffect;
typedef intptr_t (*AudioMasterCallback) (AEffect*, int32_t, int32_t, intptr_t, void*, float);
typedef intptr_t (*DispatcherProc) (AEffect*, int32_t, int32_t, intptr_t, void*, float);
typedef void (*ProcessProc) (AEffect*, float**, float**, int32_t);
typedef void (*SetParameterProc) (AEffect*, int32_t, float);
typedef float (*GetParameterProc) (AEffect*, int32_t);
struct AEffect
{
    int32_t magic;
    DispatcherProc dispatcher;
    ProcessProc process;
    SetParameterProc setParameter;
    GetParameterProc getParameter;
    int32_t numPrograms, numParams, numInputs, numOutputs, flags;
    intptr_t resvd1, resvd2;
    int32_t initialDelay, realQualities, offQualities;
    float ioRatio;
    void* object; void* user;
    int32_t uniqueID, version;
    ProcessProc processReplacing;
    void* processDoubleReplacing;
    char future[56];
};
enum { effOpen = 0, effClose = 1, effGetParamLabel = 6, effGetParamDisplay = 7, effGetParamName = 8,
       effSetSampleRate = 10, effSetBlockSize = 11, effMainsChanged = 12, effStartProcess = 71, effStopProcess = 72 };

static intptr_t hostCb (AEffect*, int32_t op, int32_t, intptr_t, void*, float)
{
    if (op == 1) return 2400;   // audioMasterVersion
    return 0;
}

static AEffect* load (const char* dll)
{
    HMODULE h = LoadLibraryA (dll);
    if (! h) { printf ("nao carregou %s (erro %lu)\n", dll, GetLastError()); return nullptr; }
    typedef AEffect* (*Main) (AudioMasterCallback);
    Main m = (Main) GetProcAddress (h, "VSTPluginMain");
    if (! m) m = (Main) GetProcAddress (h, "main");
    if (! m) { printf ("sem VSTPluginMain\n"); return nullptr; }
    AEffect* e = m (hostCb);
    if (! e || e->magic != 0x56737450) { printf ("plugin invalido\n"); return nullptr; }
    e->dispatcher (e, effOpen, 0, 0, nullptr, 0);
    e->dispatcher (e, effSetSampleRate, 0, 0, nullptr, 48000.0f);
    e->dispatcher (e, effSetBlockSize, 0, 512, nullptr, 0);
    return e;
}

static std::string pstr (AEffect* e, int op, int i) { char b[256] = {}; e->dispatcher (e, op, i, 0, b, 0); return b; }

int main (int argc, char** argv)
{
    if (argc < 3) { printf ("uso\n"); return 1; }
    std::string mode = argv[1];
    AEffect* e = load (argv[2]);
    if (! e) return 2;
    printf ("params %d  in %d out %d  delay %d  flags 0x%x\n", e->numParams, e->numInputs, e->numOutputs, e->initialDelay, e->flags);
    if (mode == "list")
    {
        for (int i = 0; i < e->numParams; ++i)
        {
            printf ("%2d  %-16s default %.4f  [%s %s]\n", i, pstr (e, effGetParamName, i).c_str(), e->getParameter (e, i),
                    pstr (e, effGetParamDisplay, i).c_str(), pstr (e, effGetParamLabel, i).c_str());
            // varre os valores para ver o mapa display <-> 0..1
            std::string sweep;
            for (int s = 0; s <= 10; ++s) { e->setParameter (e, i, s / 10.0f); sweep += pstr (e, effGetParamDisplay, i) + " | "; }
            printf ("      %s\n", sweep.c_str());
        }
        return 0;
    }
    // render
    std::ifstream fi (argv[3], std::ios::binary | std::ios::ate);
    const size_t N = (size_t) fi.tellg() / 4; fi.seekg (0);
    std::vector<float> x (N); fi.read ((char*) x.data(), (std::streamsize) (N * 4));
    std::vector<float> defaults ((size_t) e->numParams);
    for (int i = 0; i < e->numParams; ++i) defaults[(size_t) i] = e->getParameter (e, i);
    std::ifstream fs (argv[4]); std::string line;
    const int B = 512;
    std::vector<float> inL (B), inR (B), outL (B), outR (B);
    while (std::getline (fs, line))
    {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss (line); std::string name, kv; ss >> name;
        for (int i = 0; i < e->numParams; ++i) e->setParameter (e, i, defaults[(size_t) i]);
        std::vector<std::pair<int, float>> sets;
        while (ss >> kv) { auto p = kv.find ('='); sets.push_back ({ std::stoi (kv.substr (0, p)), std::stof (kv.substr (p + 1)) }); }
        // recomeça o plugin do zero a cada take
        e->dispatcher (e, effMainsChanged, 0, 0, nullptr, 0);
        for (auto& s : sets) e->setParameter (e, s.first, s.second);
        e->dispatcher (e, effMainsChanged, 0, 1, nullptr, 0);
        e->dispatcher (e, effStartProcess, 0, 0, nullptr, 0);
        // pré-roll de 1 s de silêncio (estabiliza filtros/suavizações)
        std::fill (inL.begin(), inL.end(), 0.0f); std::fill (inR.begin(), inR.end(), 0.0f);
        float* ins[2] = { inL.data(), inR.data() }; float* outs[2] = { outL.data(), outR.data() };
        for (int b = 0; b < 48000 / B; ++b) e->processReplacing (e, ins, outs, B);
        std::vector<float> y; y.reserve (N);
        for (size_t s = 0; s < N; s += B)
        {
            const int n = (int) std::min ((size_t) B, N - s);
            for (int i = 0; i < B; ++i) { const float v = i < n ? x[s + i] : 0.0f; inL[(size_t) i] = v; inR[(size_t) i] = v; }
            e->processReplacing (e, ins, outs, B);
            for (int i = 0; i < n; ++i) y.push_back (outL[(size_t) i]);
        }
        e->dispatcher (e, effStopProcess, 0, 0, nullptr, 0);
        std::string fn = std::string (argv[5]) + "/" + name + ".f32";
        FILE* fo = fopen (fn.c_str(), "wb"); fwrite (y.data(), 4, y.size(), fo); fclose (fo);
        double en = 0; float pk = 0; for (float v : y) { en += (double) v * v; pk = std::max (pk, std::abs (v)); }
        printf ("%s: rms %.1f dB pico %.2f\n", name.c_str(), 10 * log10 (en / y.size() + 1e-30), pk);
        fflush (stdout);
    }
    e->dispatcher (e, effClose, 0, 0, nullptr, 0);
    return 0;
}

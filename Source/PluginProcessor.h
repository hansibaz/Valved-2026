#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "dsp/NoiseGate.h"
#include "dsp/PitchShifter.h"
#include "dsp/TransposeShifter.h"
#include "dsp/AmpModel.h"
#include "dsp/NeuralAmp.h"
#include "dsp/LevelMatch.h"
#include "dsp/CabSim.h"
#include "dsp/Overdrive.h"
#include "dsp/Delay.h"
#include "dsp/Reverb.h"
#include "dsp/Tuner.h"
#include "dsp/ChorusPedal.h"
#include "dsp/GraphicEq.h"

namespace ParamID
{
    inline constexpr const char* input     = "input";
    inline constexpr const char* gate      = "gate";
    inline constexpr const char* transpose = "transpose";
    inline constexpr const char* amp       = "amp";
    inline constexpr const char* gain      = "gain";
    inline constexpr const char* bass      = "bass";
    inline constexpr const char* mid       = "mid";
    inline constexpr const char* treble    = "treble";
    inline constexpr const char* presence  = "presence";
    inline constexpr const char* master    = "master";
    inline constexpr const char* resonance = "resonance";
    inline constexpr const char* engine    = "engine";      // (antigo) motor do amp; hoje o motor é fixo por amp
    inline constexpr const char* dzMod     = "dzMod";       // REC: chave DZ MOD (desligada = Circuit, ligada = DZ MOD / Version 2), 2 Version 2 (só no Rec)
    inline constexpr const char* powerAmp  = "poweramp";    // Rec V2: Power Amp
    inline constexpr const char* ampPower   = "ampPower";   // chave POWER do amp (desligado = sem som)
    inline constexpr const char* ampStandby = "ampStandby"; // chave STANDBY (para baixo = válvulas quentes, sem som)

    // Cada amp guarda a própria regulagem: "a0_gain", "a1_gain", ...
    inline juce::String ampKnob (int model, const char* knob) { return "a" + juce::String (model) + "_" + knob; }
    inline constexpr const char* cabOn     = "cabOn";
    inline constexpr const char* cab       = "cab";
    inline constexpr const char* micX      = "micX";
    inline constexpr const char* micDist   = "micDist";
    inline constexpr const char* mic       = "mic";          // modelo do microfone
    inline constexpr const char* micAngle  = "micAngle";     // graus fora do eixo
    inline constexpr const char* micBassCut  = "micBassCut"; // SM7B: bass rolloff
    inline constexpr const char* micPresence = "micPresence";// SM7B: presence boost
    inline constexpr const char* md421Bass   = "md421Bass";  // MD421: chave de graves M..S
    inline constexpr const char* output    = "output";

    // Pedais (prefixos: green, yellow, gold, blue + On/Drive/Tone/Level)
    inline juce::String pedal (int index, const char* suffix)
    {
        static const char* names[] = { "green", "yellow", "gold", "blue" };
        return juce::String (names[juce::jlimit (0, 3, index)]) + suffix;
    }

    inline constexpr const char* greenMode   = "greenMode";    // TS808 / TS9
    inline constexpr const char* quality     = "quality";
    inline constexpr const char* eqOn        = "eqOn";         // pedal de EQ (post FX)
    inline constexpr const char* eqMode      = "eqMode";       // 0 VALVED (7 bandas), 1 MARK (5 bandas)
    inline constexpr const char* eqLevel     = "eqLevel";
    inline juce::String eqBand (int mode, int band) { return juce::String (mode == 1 ? "eqM" : "eqV") + juce::String (band); }      // 0 baixa, 1 média, 2 alta
    inline constexpr const char* ceOn        = "ceOn";         // pedal de chorus (Ensemble, CE-1)
    inline constexpr const char* ceMode      = "ceMode";       // 0 chorus, 1 vibrato
    inline constexpr const char* ceLevel     = "ceLevel";
    inline constexpr const char* ceIntensity = "ceIntensity";
    inline constexpr const char* ceDepth     = "ceDepth";
    inline constexpr const char* ceRate      = "ceRate";
    inline constexpr const char* compOn      = "compOn";
    inline constexpr const char* compSustain = "compSustain";
    inline constexpr const char* compLevel   = "compLevel";
    inline constexpr const char* compAttack  = "compAttack";
    inline constexpr const char* compClip    = "compClip";

    inline constexpr const char* dlyOn      = "dlyOn";
    inline constexpr const char* dlyType    = "dlyType";
    inline constexpr const char* dlyTime    = "dlyTime";
    inline constexpr const char* dlySync    = "dlySync";
    inline constexpr const char* dlyDiv     = "dlyDiv";
    inline constexpr const char* dlyRepeats = "dlyRepeats";
    inline constexpr const char* dlyMix     = "dlyMix";
    inline constexpr const char* dlyTone    = "dlyTone";
    inline constexpr const char* dlyMod     = "dlyMod";
    inline constexpr const char* dlyParam   = "dlyParam";

    inline constexpr const char* revOn      = "revOn";
    inline constexpr const char* revType    = "revType";
    inline constexpr const char* revDecay   = "revDecay";
    inline constexpr const char* revPre     = "revPre";
    inline constexpr const char* revMix     = "revMix";
    inline constexpr const char* revTone    = "revTone";
    inline constexpr const char* revMod     = "revMod";
    inline constexpr const char* revParam   = "revParam";
}

class ValvedProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    ValvedProcessor();
    ~ValvedProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Valved"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Carrega um IR do usuário (chamado pela interface)
    bool loadUserIR (const juce::File& file);
    juce::String getUserIRName() const;

    float getInputLevel() const  { return inputMeter.load(); }
    float getOutputLevel() const { return outputMeter.load(); }
    float getGateGain() const    { return gateMeter.load(); }
    double getBpm() const        { return bpm.load(); }

    // Afinador: o áudio da guitarra vai para a fila; a análise roda na interface
    vlv::TunerFeed tunerFeed;
    std::atomic<bool> tunerActive { false }, tunerMute { true };
    std::atomic<double> tunerRef { 440.0 };

    // Divisões de tempo do delay sincronizado
    static juce::StringArray divisionNames() { return { "1/2", "1/4.", "1/4", "1/4T", "1/8.", "1/8", "1/8T", "1/16" }; }
    static double divisionBeats (int i)
    {
        static const double b[] = { 2.0, 1.5, 1.0, 2.0 / 3.0, 0.75, 0.5, 1.0 / 3.0, 0.25 };
        return b[juce::jlimit (0, 7, i)];
    }
    float getEffectiveDelayMs() const;

    juce::AudioProcessorValueTreeState apvts;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    void timerCallback() override;
    void refreshCab (bool force);

    vlv::NoiseGate gate;
    vlv::TransposeShifter shifter;
    std::vector<float> gateGains, gateDelay; int gateDelayW = 0;
    vlv::AmpModel amp;
    vlv::NeuralAmp neuralAmp;
    juce::SmoothedValue<float> levelMatch { 1.0f };   // volume igual entre amps + mesma curva do Master
    vlv::OutputLimiter limiter;                       // teto de -1 dBFS na saída
    juce::SmoothedValue<float> tunerGain { 1.0f };    // mudo do afinador sem estalo
    juce::SmoothedValue<float> ampOnGain { 1.0f };    // POWER / STANDBY
    std::atomic<float>* pAmpPower = nullptr; std::atomic<float>* pAmpStandby = nullptr;
    int lastEngine = -1;
public:
    enum Engine { EngineCircuit = 0, EngineNeural = 1, EngineV2 = 2 };
    // Version 2 só existe no Rec; nos outros amps vale como Neural
    // Motor fixo por amp: Clean 120 = Circuit, Brit80 e 51 Block = Neural, REC = Circuit ou DZ MOD (Version 2)
    int activeEngine() const
    {
        switch ((int) pAmp->load())
        {
            case 0:  return EngineCircuit;
            case 3:  return pDzMod->load() > 0.5f ? EngineV2 : EngineCircuit;
            default: return EngineNeural;
        }
    }
    static constexpr int RecV2Slot = 4;   // knobs próprios do Rec V2 ("a4_...")
private:
    vlv::pedal::EnsembleChorus ensemble;
    std::atomic<float>* pQuality = nullptr;
    std::atomic<float>* pEqOn = nullptr; std::atomic<float>* pEqMode = nullptr; std::atomic<float>* pEqLevel = nullptr;
    std::atomic<float>* pEqV[7] {}; std::atomic<float>* pEqM[5] {};
    vlv::GraphicEq graphicEq;


    int appliedQuality = 1;
    vlv::CabSim cab;
    vlv::Overdrive pedals[4] { vlv::Overdrive (vlv::PedalGreen), vlv::Overdrive (vlv::PedalYellow),
                               vlv::Overdrive (vlv::PedalGold), vlv::Overdrive (vlv::PedalBlue) };
    vlv::pedal::OTACompressor compressor;
    vlv::StereoDelay delay;
    vlv::AmbientReverb reverb;
    std::atomic<double> bpm { 120.0 };

    juce::AudioBuffer<float> mono;
    juce::SmoothedValue<float> inGain, outGain;

    std::atomic<float>* pInput; std::atomic<float>* pGate; std::atomic<float>* pTranspose;
    std::atomic<float>* pAmp; std::atomic<float>* pGain; std::atomic<float>* pBass;
    std::atomic<float>* pMid; std::atomic<float>* pTreble; std::atomic<float>* pPresence;
    std::atomic<float>* pMaster; std::atomic<float>* pCabOn;
    std::atomic<float>* pCab; std::atomic<float>* pMicX; std::atomic<float>* pMicDist;
    std::atomic<float>* pMic; std::atomic<float>* pMicAngle; std::atomic<float>* pMicBassCut; std::atomic<float>* pMicPresence; std::atomic<float>* pMd421Bass;
    std::atomic<float>* pOutput;
    std::atomic<float>* pCe[6];
    std::atomic<float>* pResonance;
    std::atomic<float>* pEngine;
    std::atomic<float>* pDzMod = nullptr;
    std::atomic<float>* pAmpKnob[5][8] {};   // [amp][gain, bass, mid, treble, presence, master, resonance, poweramp]; amp 4 = Rec V2
    std::atomic<float>* pPedal[4][4];   // [pedal][on, drive, tone, level]
    std::atomic<float>* pDly[10];
    std::atomic<float>* pComp[5];   // on, sustain, level, attack, clip
    std::atomic<float>* pGreenMode;
    std::atomic<float>* pRev[8];

    // Estado do gabinete (lido no timer, na thread da interface)
    int lastCab = -1; float lastMicX = -1, lastMicDist = -1;
    int lastMic = -1; float lastMicAngle = -1; bool lastBassCut = false, lastPresence = false; int lastMd421Bass = -1;
    juce::File userIRFile;
    bool userIRLoaded = false;
    std::atomic<bool> prepared { false };
    mutable juce::CriticalSection irLock;

    std::atomic<float> inputMeter { 0 }, outputMeter { 0 }, gateMeter { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ValvedProcessor)
};

#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "dsp/NoiseGate.h"
#include "dsp/PitchShifter.h"
#include "dsp/TransposeShifter.h"
#include "dsp/AmpModel.h"
#include "dsp/CabSim.h"
#include "dsp/Overdrive.h"
#include "dsp/Delay.h"
#include "dsp/Reverb.h"

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
    inline constexpr const char* chorus    = "chorus";
    inline constexpr const char* chorusRate  = "chorusRate";
    inline constexpr const char* chorusDepth = "chorusDepth";
    inline constexpr const char* resonance = "resonance";
    inline constexpr const char* recMode   = "recMode";
    inline constexpr const char* britMode  = "britMode";

    // Cada amp guarda a própria regulagem: "a0_gain", "a1_gain", ...
    inline juce::String ampKnob (int model, const char* knob) { return "a" + juce::String (model) + "_" + knob; }
    inline constexpr const char* cabOn     = "cabOn";
    inline constexpr const char* cab       = "cab";
    inline constexpr const char* micX      = "micX";
    inline constexpr const char* micDist   = "micDist";
    inline constexpr const char* output    = "output";

    // Pedais (prefixos: green, yellow, gold, blue + On/Drive/Tone/Level)
    inline juce::String pedal (int index, const char* suffix)
    {
        static const char* names[] = { "green", "yellow", "gold", "blue" };
        return juce::String (names[juce::jlimit (0, 3, index)]) + suffix;
    }

    inline constexpr const char* greenMode   = "greenMode";    // TS808 / TS9
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
    vlv::CE1Chorus chorus;
    vlv::CabSim cab;
    vlv::Overdrive pedals[4] { vlv::Overdrive (vlv::PedalGreen), vlv::Overdrive (vlv::PedalYellow),
                               vlv::Overdrive (vlv::PedalGold), vlv::Overdrive (vlv::PedalBlue) };
    vlv::pedal::OTACompressor compressor;
    vlv::StereoDelay delay;
    vlv::AmbientReverb reverb;
    std::atomic<double> bpm { 120.0 };

    juce::AudioBuffer<float> mono;
    juce::SmoothedValue<float> inGain, outGain;
    float chorusMix = 0.0f;

    std::atomic<float>* pInput; std::atomic<float>* pGate; std::atomic<float>* pTranspose;
    std::atomic<float>* pAmp; std::atomic<float>* pGain; std::atomic<float>* pBass;
    std::atomic<float>* pMid; std::atomic<float>* pTreble; std::atomic<float>* pPresence;
    std::atomic<float>* pMaster; std::atomic<float>* pChorus; std::atomic<float>* pCabOn;
    std::atomic<float>* pCab; std::atomic<float>* pMicX; std::atomic<float>* pMicDist;
    std::atomic<float>* pOutput;
    std::atomic<float>* pChorusRate; std::atomic<float>* pChorusDepth;
    std::atomic<float>* pResonance; std::atomic<float>* pRecMode; std::atomic<float>* pBritMode;
    std::atomic<float>* pAmpKnob[4][7] {};   // [amp][gain, bass, mid, treble, presence, master, resonance]
    std::atomic<float>* pPedal[4][4];   // [pedal][on, drive, tone, level]
    std::atomic<float>* pDly[10];
    std::atomic<float>* pComp[5];   // on, sustain, level, attack, clip
    std::atomic<float>* pGreenMode;
    std::atomic<float>* pRev[8];

    // Estado do gabinete (lido no timer, na thread da interface)
    int lastCab = -1; float lastMicX = -1, lastMicDist = -1;
    juce::File userIRFile;
    bool userIRLoaded = false;
    std::atomic<bool> prepared { false };
    mutable juce::CriticalSection irLock;

    std::atomic<float> inputMeter { 0 }, outputMeter { 0 }, gateMeter { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ValvedProcessor)
};

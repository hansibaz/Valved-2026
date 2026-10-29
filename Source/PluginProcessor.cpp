#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout ValvedProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto knob = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name,
            NormalisableRange<float> (0.0f, 10.0f, 0.01f), def,
            AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float v, int) { return String (v, 1); }));
    };
    auto dbParam = [] (const char* id, const char* name, float lo, float hi, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name,
            NormalisableRange<float> (lo, hi, 0.1f), def,
            AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (
                [] (float v, int) { return String (v, 1) + " dB"; }));
    };

    p.push_back (dbParam (ParamID::input, "Input", -24.0f, 24.0f, 0.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::gate, 1 }, "Noise Gate",
        NormalisableRange<float> (-90.0f, -20.0f, 0.1f), -90.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return v <= -89.0f ? String ("Off") : String (v, 0) + " dB"; })));
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { ParamID::transpose, 1 }, "Transpose",
        -12, 12, 0, AudioParameterIntAttributes().withStringFromValueFunction (
            [] (int v, int) { return (v > 0 ? "+" : "") + String (v) + " st"; })));

    StringArray ampNames;
    for (auto& v : vlv::getVoicings()) ampNames.add (v.name);
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::amp, 1 }, "Amp", ampNames, 1));

    p.push_back (knob (ParamID::gain, "Gain", 5.0f));
    p.push_back (knob (ParamID::bass, "Bass", 5.0f));
    p.push_back (knob (ParamID::mid, "Mid", 5.0f));
    p.push_back (knob (ParamID::treble, "Treble", 5.0f));
    p.push_back (knob (ParamID::presence, "Presence", 5.0f));
    p.push_back (knob (ParamID::master, "Master", 5.0f));
    p.push_back (knob (ParamID::resonance, "Resonance", 5.0f));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::recMode, 1 }, "Rec Mode",
        StringArray { "Vintage", "Modern" }, 1));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ParamID::chorus, 1 }, "Chorus", false));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::chorusRate, 1 }, "Chorus Rate",
        NormalisableRange<float> (0.1f, 6.0f, 0.01f, 0.5f), 0.8f,
        AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (
            [] (float v, int) { return String (v, 2) + " Hz"; })));
    p.push_back (knob (ParamID::chorusDepth, "Chorus Depth", 5.0f));

    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ParamID::cabOn, 1 }, "Cab On", true));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::cab, 1 }, "Cabinet", vlv::getCabNames(), 0));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::micX, 1 }, "Mic Position",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.25f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::micDist, 1 }, "Mic Distance",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.1f));

    p.push_back (dbParam (ParamID::output, "Output", -36.0f, 12.0f, 0.0f));

    // ---- Pedais (antes do amp)
    static const char* pedalLabels[] = { "Green Drive", "Yellow Drive", "Gold Drive", "Blue Drive" };
    for (int i = 0; i < 4; ++i)
    {
        const String nm (pedalLabels[i]);
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ParamID::pedal (i, "On"), 1 }, nm + " On", false));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::pedal (i, "Drive"), 1 }, nm + " Drive",
            NormalisableRange<float> (0.0f, 10.0f, 0.01f), i == 2 ? 3.0f : 4.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1); })));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::pedal (i, "Tone"), 1 }, nm + " Tone",
            NormalisableRange<float> (0.0f, 10.0f, 0.01f), 5.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1); })));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::pedal (i, "Level"), 1 }, nm + " Level",
            NormalisableRange<float> (0.0f, 10.0f, 0.01f), 6.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1); })));
    }

    // ---- Delay
    auto pct = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 100.0f, 0.1f), def,
            AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return String (v, 0) + " %"; }));
    };
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ParamID::dlyOn, 1 }, "Delay On", false));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::dlyType, 1 }, "Delay Type", vlv::StereoDelay::typeNames(), 0));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::dlyTime, 1 }, "Delay Time",
        NormalisableRange<float> (20.0f, 2000.0f, 1.0f, 0.45f), 420.0f,
        AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction ([] (float v, int) { return String (v, 0) + " ms"; })));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ParamID::dlySync, 1 }, "Delay Sync", false));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::dlyDiv, 1 }, "Delay Division", divisionNames(), 2));
    p.push_back (knob (ParamID::dlyRepeats, "Delay Repeats", 4.0f));
    p.push_back (pct (ParamID::dlyMix, "Delay Mix", 30.0f));
    p.push_back (knob (ParamID::dlyTone, "Delay Tone", 6.0f));
    p.push_back (knob (ParamID::dlyMod, "Delay Mod", 2.0f));
    p.push_back (knob (ParamID::dlyParam, "Delay Character", 5.0f));

    // ---- Reverb
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ParamID::revOn, 1 }, "Reverb On", false));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::revType, 1 }, "Reverb Type", vlv::AmbientReverb::typeNames(), 1));
    p.push_back (knob (ParamID::revDecay, "Reverb Decay", 5.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::revPre, 1 }, "Reverb Pre-Delay",
        NormalisableRange<float> (0.0f, 250.0f, 0.1f, 0.6f), 20.0f,
        AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction ([] (float v, int) { return String (v, 0) + " ms"; })));
    p.push_back (pct (ParamID::revMix, "Reverb Mix", 25.0f));
    p.push_back (knob (ParamID::revTone, "Reverb Tone", 6.0f));
    p.push_back (knob (ParamID::revMod, "Reverb Mod", 3.0f));
    p.push_back (knob (ParamID::revParam, "Reverb Character", 5.0f));

    return { p.begin(), p.end() };
}

ValvedProcessor::ValvedProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VALVED", createLayout())
{
    pInput     = apvts.getRawParameterValue (ParamID::input);
    pGate      = apvts.getRawParameterValue (ParamID::gate);
    pTranspose = apvts.getRawParameterValue (ParamID::transpose);
    pAmp       = apvts.getRawParameterValue (ParamID::amp);
    pGain      = apvts.getRawParameterValue (ParamID::gain);
    pBass      = apvts.getRawParameterValue (ParamID::bass);
    pMid       = apvts.getRawParameterValue (ParamID::mid);
    pTreble    = apvts.getRawParameterValue (ParamID::treble);
    pPresence  = apvts.getRawParameterValue (ParamID::presence);
    pMaster    = apvts.getRawParameterValue (ParamID::master);
    pChorus    = apvts.getRawParameterValue (ParamID::chorus);
    pCabOn     = apvts.getRawParameterValue (ParamID::cabOn);
    pCab       = apvts.getRawParameterValue (ParamID::cab);
    pMicX      = apvts.getRawParameterValue (ParamID::micX);
    pMicDist   = apvts.getRawParameterValue (ParamID::micDist);
    pOutput    = apvts.getRawParameterValue (ParamID::output);
    pChorusRate  = apvts.getRawParameterValue (ParamID::chorusRate);
    pChorusDepth = apvts.getRawParameterValue (ParamID::chorusDepth);
    pResonance   = apvts.getRawParameterValue (ParamID::resonance);
    pRecMode     = apvts.getRawParameterValue (ParamID::recMode);

    for (int i = 0; i < 4; ++i)
    {
        int k = 0;
        for (auto* suffix : { "On", "Drive", "Tone", "Level" })
            pPedal[i][k++] = apvts.getRawParameterValue (ParamID::pedal (i, suffix));
    }
    {
        const char* ids[] = { ParamID::dlyOn, ParamID::dlyType, ParamID::dlyTime, ParamID::dlySync, ParamID::dlyDiv,
                              ParamID::dlyRepeats, ParamID::dlyMix, ParamID::dlyTone, ParamID::dlyMod, ParamID::dlyParam };
        for (int k = 0; k < 10; ++k) pDly[k] = apvts.getRawParameterValue (ids[k]);
        const char* rids[] = { ParamID::revOn, ParamID::revType, ParamID::revDecay, ParamID::revPre,
                               ParamID::revMix, ParamID::revTone, ParamID::revMod, ParamID::revParam };
        for (int k = 0; k < 8; ++k) pRev[k] = apvts.getRawParameterValue (rids[k]);
    }

    startTimerHz (20);
}

ValvedProcessor::~ValvedProcessor() { stopTimer(); }

bool ValvedProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    const bool inOk  = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return inOk && outOk;
}

void ValvedProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    prepared = false;
    mono.setSize (1, juce::jmax (1, samplesPerBlock), false, true, false);

    gate.prepare (sampleRate);
    shifter.prepare (sampleRate);
    amp.prepare (sampleRate, samplesPerBlock);
    chorus.prepare (sampleRate);
    for (auto& pd : pedals) pd.prepare (sampleRate, samplesPerBlock);
    delay.prepare (sampleRate);
    reverb.prepare (sampleRate);

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (1, samplesPerBlock),
                                  (juce::uint32) juce::jmax (1, getTotalNumOutputChannels()) };
    cab.prepare (spec);

    inGain.reset (sampleRate, 0.03);
    outGain.reset (sampleRate, 0.03);
    inGain.setCurrentAndTargetValue (vlv::dbToGain (pInput->load()));
    outGain.setCurrentAndTargetValue (vlv::dbToGain (pOutput->load()));
    chorusMix = pChorus->load() > 0.5f ? 1.0f : 0.0f;

    {
        const juce::ScopedLock sl (irLock);
        userIRLoaded = false;
        lastCab = -1;
    }
    refreshCab (true);
    prepared = true;
}

void ValvedProcessor::refreshCab (bool force)
{
    const juce::ScopedLock sl (irLock);
    const int type = (int) pCab->load();
    const float mx = pMicX->load(), md = pMicDist->load();

    if (type == vlv::CabUserIR)
    {
        if ((force || lastCab != type || ! userIRLoaded) && userIRFile.existsAsFile())
        {
            cab.loadFile (userIRFile);
            userIRLoaded = true;
        }
        lastCab = type;
        return;
    }

    if (force || type != lastCab || std::abs (mx - lastMicX) > 0.002f || std::abs (md - lastMicDist) > 0.002f)
    {
        cab.loadBuiltIn (type, mx, md);
        lastCab = type; lastMicX = mx; lastMicDist = md;
    }
}

void ValvedProcessor::timerCallback()
{
    if (prepared) refreshCab (false);
}

bool ValvedProcessor::loadUserIR (const juce::File& file)
{
    if (! file.existsAsFile()) return false;
    {
        const juce::ScopedLock sl (irLock);
        userIRFile = file;
        userIRLoaded = false;
    }
    if (auto* p = apvts.getParameter (ParamID::cab))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) vlv::CabUserIR));
        p->endChangeGesture();
    }
    if (prepared) refreshCab (true);
    return true;
}

juce::String ValvedProcessor::getUserIRName() const
{
    const juce::ScopedLock sl (irLock);
    return userIRFile.existsAsFile() ? userIRFile.getFileNameWithoutExtension() : juce::String();
}

void ValvedProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    if (n == 0) return;

    if (mono.getNumSamples() < n)
        mono.setSize (1, n, false, false, true);

    // 1) Soma a entrada em mono (guitarra)
    float* m = mono.getWritePointer (0);
    if (numIn > 0) juce::FloatVectorOperations::copy (m, buffer.getReadPointer (0), n);
    else juce::FloatVectorOperations::clear (m, n);
    for (int c = 1; c < numIn; ++c)
        juce::FloatVectorOperations::add (m, buffer.getReadPointer (c), n);

    // 2) Ganho de entrada
    inGain.setTargetValue (vlv::dbToGain (pInput->load()));
    float inPeak = 0;
    for (int i = 0; i < n; ++i)
    {
        m[i] *= inGain.getNextValue();
        inPeak = juce::jmax (inPeak, std::abs (m[i]));
    }
    inputMeter = juce::jmax (inPeak, inputMeter.load() * 0.8f);

    // 3) Noise gate -> 4) Transpose -> 5) Amp
    gate.setThresholdDb (pGate->load());
    shifter.setSemitones ((int) std::lround (pTranspose->load()));
    for (int i = 0; i < n; ++i)
    {
        m[i] = gate.process (m[i]);
        m[i] = shifter.process (m[i]);
    }
    gateMeter = gate.getGain();

    // 5) Pedais de overdrive (Green -> Yellow -> Gold -> Blue), antes do amp
    for (int i = 0; i < 4; ++i)
    {
        pedals[i].setParams (pPedal[i][0]->load() > 0.5f, pPedal[i][1]->load(), pPedal[i][2]->load(), pPedal[i][3]->load());
        pedals[i].process (m, n);
    }

    vlv::AmpModel::Settings st;
    st.model = (int) pAmp->load();
    st.gain = pGain->load(); st.bass = pBass->load(); st.mid = pMid->load(); st.treble = pTreble->load();
    st.presence = pPresence->load(); st.master = pMaster->load(); st.resonance = pResonance->load();
    st.recModern = pRecMode->load() > 0.5f;
    amp.setParameters (st);
    chorus.setRate (pChorusRate->load());
    chorus.setDepth (pChorusDepth->load() / 10.0f);
    amp.process (m, n);

    // 6) Divide em estéreo com o chorus (lado esquerdo seco, direito com chorus)
    // O chorus faz parte do Jazz 120 (só funciona nele)
    const float chorusTarget = (pChorus->load() > 0.5f && (int) pAmp->load() == 0) ? 1.0f : 0.0f;
    const float chorusStep = 1.0f / (0.02f * (float) getSampleRate());
    float* L = buffer.getWritePointer (0);
    float* R = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
    for (int i = 0; i < n; ++i)
    {
        float l, r;
        chorus.process (m[i], l, r);
        if (chorusMix < chorusTarget) chorusMix = juce::jmin (chorusTarget, chorusMix + chorusStep);
        else if (chorusMix > chorusTarget) chorusMix = juce::jmax (chorusTarget, chorusMix - chorusStep);
        const float right = m[i] + (r - m[i]) * chorusMix;
        if (R != nullptr) { L[i] = l; R[i] = right; }
        else L[i] = 0.5f * (l + right);
    }
    for (int c = 2; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);

    // 7) Gabinete / IR
    if (pCabOn->load() > 0.5f)
    {
        juce::dsp::AudioBlock<float> block (buffer);
        auto sub = block.getSubsetChannelBlock (0, (size_t) juce::jmin (numOut, 2));
        cab.process (sub);
    }

    // 8) Delay e reverb (estéreo, depois do gabinete)
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                if (*b > 20.0 && *b < 400.0) bpm = *b;
    {
        float* Lp = buffer.getWritePointer (0);
        float* Rp = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
        vlv::StereoDelay::Settings ds;
        ds.on = pDly[0]->load() > 0.5f; ds.type = (int) pDly[1]->load(); ds.timeMs = getEffectiveDelayMs();
        ds.repeats = pDly[5]->load(); ds.mix = pDly[6]->load(); ds.tone = pDly[7]->load();
        ds.mod = pDly[8]->load(); ds.param = pDly[9]->load();
        delay.process (Lp, Rp, n, ds);

        vlv::AmbientReverb::Settings rs;
        rs.on = pRev[0]->load() > 0.5f; rs.type = (int) pRev[1]->load(); rs.decay = pRev[2]->load();
        rs.preDelayMs = pRev[3]->load(); rs.mix = pRev[4]->load(); rs.tone = pRev[5]->load();
        rs.mod = pRev[6]->load(); rs.param = pRev[7]->load();
        reverb.process (Lp, Rp, n, rs);
    }

    // 9) Volume de saída
    outGain.setTargetValue (vlv::dbToGain (pOutput->load()));
    float outPeak = 0;
    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.getNextValue();
        L[i] *= g;
        outPeak = juce::jmax (outPeak, std::abs (L[i]));
        if (R != nullptr) { R[i] *= g; outPeak = juce::jmax (outPeak, std::abs (R[i])); }
    }
    outputMeter = juce::jmax (outPeak, outputMeter.load() * 0.8f);
}

void ValvedProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    {
        const juce::ScopedLock sl (irLock);
        state.setProperty ("userIR", userIRFile.getFullPathName(), nullptr);
    }
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ValvedProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        if (tree.isValid() && tree.hasType (apvts.state.getType()))
        {
            const juce::String irPath = tree.getProperty ("userIR").toString();
            apvts.replaceState (tree);
            {
                const juce::ScopedLock sl (irLock);
                userIRFile = irPath.isNotEmpty() ? juce::File (irPath) : juce::File();
                userIRLoaded = false;
            }
            if (prepared) refreshCab (true);
        }
    }
}

float ValvedProcessor::getEffectiveDelayMs() const
{
    if (pDly[3]->load() > 0.5f)
        return (float) juce::jlimit (20.0, 2000.0, 60000.0 / bpm.load() * divisionBeats ((int) pDly[4]->load()));
    return pDly[2]->load();
}

juce::AudioProcessorEditor* ValvedProcessor::createEditor()
{
    return new ValvedEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ValvedProcessor();
}

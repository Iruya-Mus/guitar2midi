#pragma once

#include "../dsp/GuitarToMidiEngine.h"
#include "MidiPortOutput.h"

#include <juce_audio_processors/juce_audio_processors.h>

class Guitar2MidiProcessor final : public juce::AudioProcessor
{
public:
    Guitar2MidiProcessor();
    ~Guitar2MidiProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- used by the editor (message thread) ---
    juce::AudioProcessorValueTreeState params;
    g2m::GuitarToMidiEngine engine;
    MidiPortOutput portOut;

    bool selectPort (const juce::String& identifier); // empty = none
    void autoSelectPort();                            // picks a port named "loopMIDI..." if none chosen

    static juce::String noteName (int note);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    g2m::EngineSettings readSettings() const;
    void emit (const g2m::NoteEventBuffer& events, int channel, juce::MidiBuffer& midi, int numSamples);

    g2m::NoteEventBuffer events;
    juce::AudioBuffer<float> monoBuffer;
    int currentChannel = 1;
    bool wasBypassed = false;
    bool userChosePort = false;

    std::atomic<float>* pGate {};
    std::atomic<float>* pOnset {};
    std::atomic<float>* pTolerance {};
    std::atomic<float>* pStability {};
    std::atomic<float>* pRelease {};
    std::atomic<float>* pLowest {};
    std::atomic<float>* pHighest {};
    std::atomic<float>* pTranspose {};
    std::atomic<float>* pVelSens {};
    std::atomic<float>* pBend {};
    std::atomic<float>* pBendRange {};
    std::atomic<float>* pChannel {};
    std::atomic<float>* pPassAudio {};

    JUCE_DECLARE_WEAK_REFERENCEABLE (Guitar2MidiProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Guitar2MidiProcessor)
};

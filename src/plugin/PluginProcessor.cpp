#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace ids
{
    static const juce::String gate      = "gate";
    static const juce::String onset     = "onset";
    static const juce::String tolerance = "tolerance";
    static const juce::String stability = "stability";
    static const juce::String release   = "release";
    static const juce::String lowest    = "lowest";
    static const juce::String highest   = "highest";
    static const juce::String transpose = "transpose";
    static const juce::String velSens   = "velsens";
    static const juce::String bend      = "bend";
    static const juce::String bendRange = "bendrange";
    static const juce::String channel   = "channel";
    static const juce::String passAudio = "passaudio";
}

juce::String Guitar2MidiProcessor::noteName (int note)
{
    return juce::MidiMessage::getMidiNoteName (note, true, true, 4);
}

juce::AudioProcessorValueTreeState::ParameterLayout Guitar2MidiProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto dB = AudioParameterFloatAttributes().withLabel ("dB");
    auto noteAttr = AudioParameterIntAttributes()
                        .withStringFromValueFunction ([] (int v, int) { return noteName (v); });

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::gate, 1 }, "Gate",
                    NormalisableRange<float> (-80.0f, -10.0f, 0.5f), -45.0f, dB));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::onset, 1 }, "Pick Detect",
                    NormalisableRange<float> (3.0f, 24.0f, 0.5f), 8.0f, dB));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::tolerance, 1 }, "Pitch Tolerance",
                    NormalisableRange<float> (0.05f, 0.35f, 0.01f), 0.15f));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ids::stability, 1 }, "Stability", 1, 8, 2,
                    AudioParameterIntAttributes().withLabel ("frames")));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ids::release, 1 }, "Release", 2, 50, 12,
                    AudioParameterIntAttributes().withLabel ("frames")));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ids::lowest, 1 }, "Lowest Note", 28, 64, 40, noteAttr));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ids::highest, 1 }, "Highest Note", 52, 100, 88, noteAttr));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ids::transpose, 1 }, "Transpose", -36, 36, 0,
                    AudioParameterIntAttributes().withLabel ("st")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::velSens, 1 }, "Dynamics",
                    NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.8f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::bend, 1 }, "Pitch Bend", false));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ids::bendRange, 1 }, "Bend Range", 1, 24, 2,
                    AudioParameterIntAttributes().withLabel ("st")));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ids::channel, 1 }, "MIDI Channel", 1, 16, 1));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::passAudio, 1 }, "Pass Audio", true));
    return layout;
}

Guitar2MidiProcessor::Guitar2MidiProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "Guitar2MIDI", createLayout())
{
    pGate      = params.getRawParameterValue (ids::gate);
    pOnset     = params.getRawParameterValue (ids::onset);
    pTolerance = params.getRawParameterValue (ids::tolerance);
    pStability = params.getRawParameterValue (ids::stability);
    pRelease   = params.getRawParameterValue (ids::release);
    pLowest    = params.getRawParameterValue (ids::lowest);
    pHighest   = params.getRawParameterValue (ids::highest);
    pTranspose = params.getRawParameterValue (ids::transpose);
    pVelSens   = params.getRawParameterValue (ids::velSens);
    pBend      = params.getRawParameterValue (ids::bend);
    pBendRange = params.getRawParameterValue (ids::bendRange);
    pChannel   = params.getRawParameterValue (ids::channel);
    pPassAudio = params.getRawParameterValue (ids::passAudio);

    autoSelectPort();
}

Guitar2MidiProcessor::~Guitar2MidiProcessor() = default;

bool Guitar2MidiProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;
    return out == in;
}

void Guitar2MidiProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    monoBuffer.setSize (1, juce::jmax (1, samplesPerBlock));
    setLatencySamples (0); // MIDI goes out as soon as it is detected
}

void Guitar2MidiProcessor::releaseResources()
{
    engine.allNotesOff (events);
    juce::MidiBuffer dummy;
    emit (events, currentChannel, dummy, 1);
}

g2m::EngineSettings Guitar2MidiProcessor::readSettings() const
{
    g2m::EngineSettings s;
    s.gateDb        = pGate->load();
    s.onsetDb       = pOnset->load();
    s.yinThreshold  = pTolerance->load();
    s.stableFrames  = (int) pStability->load();
    s.releaseFrames = (int) pRelease->load();
    s.lowestNote    = (int) pLowest->load();
    s.highestNote   = juce::jmax (s.lowestNote + 1, (int) pHighest->load());
    s.transpose     = (int) pTranspose->load();
    s.velocitySens  = pVelSens->load();
    s.pitchBend     = pBend->load() > 0.5f;
    s.bendRange     = (int) pBendRange->load();
    return s;
}

void Guitar2MidiProcessor::emit (const g2m::NoteEventBuffer& ev, int channel,
                                 juce::MidiBuffer& midi, int numSamples)
{
    for (int i = 0; i < ev.count; ++i)
    {
        const auto& e = ev.events[(size_t) i];
        juce::MidiMessage m;
        switch (e.type)
        {
            case g2m::NoteEvent::Type::NoteOn:    m = juce::MidiMessage::noteOn (channel, e.note, (juce::uint8) e.velocity); break;
            case g2m::NoteEvent::Type::NoteOff:   m = juce::MidiMessage::noteOff (channel, e.note); break;
            case g2m::NoteEvent::Type::PitchBend: m = juce::MidiMessage::pitchWheel (channel, e.bendValue); break;
        }
        midi.addEvent (m, juce::jlimit (0, juce::jmax (0, numSamples - 1), e.sampleOffset));
        portOut.push (m);
    }
}

void Guitar2MidiProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = getTotalNumInputChannels();
    midi.clear();
    wasBypassed = false;

    // If the MIDI channel changed, release notes on the old channel first.
    const int channel = (int) pChannel->load();
    if (channel != currentChannel)
    {
        engine.allNotesOff (events);
        emit (events, currentChannel, midi, numSamples);
        currentChannel = channel;
    }

    engine.setSettings (readSettings());

    // Analyse the average of the input channels.
    if (monoBuffer.getNumSamples() < numSamples)
        monoBuffer.setSize (1, numSamples, false, false, true); // only if the host lied about block size
    auto* mono = monoBuffer.getWritePointer (0);
    if (numChannels <= 1)
        juce::FloatVectorOperations::copy (mono, buffer.getReadPointer (0), numSamples);
    else
    {
        juce::FloatVectorOperations::copy (mono, buffer.getReadPointer (0), numSamples);
        for (int ch = 1; ch < numChannels; ++ch)
            juce::FloatVectorOperations::add (mono, buffer.getReadPointer (ch), numSamples);
        juce::FloatVectorOperations::multiply (mono, 1.0f / (float) numChannels, numSamples);
    }

    engine.process (mono, numSamples, events);
    emit (events, currentChannel, midi, numSamples);

    for (int ch = numChannels; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);
    if (pPassAudio->load() < 0.5f)
        buffer.clear();
}

void Guitar2MidiProcessor::processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer& midi)
{
    midi.clear();
    if (! wasBypassed)
    {
        engine.allNotesOff (events);
        emit (events, currentChannel, midi, 1);
        wasBypassed = true;
    }
}

bool Guitar2MidiProcessor::selectPort (const juce::String& identifier)
{
    userChosePort = true;
    return portOut.open (identifier);
}

void Guitar2MidiProcessor::autoSelectPort()
{
    if (portOut.isOpen() || userChosePort)
        return;

    for (const auto& d : juce::MidiOutput::getAvailableDevices())
        if (d.name.containsIgnoreCase ("loopMIDI") || d.name.containsIgnoreCase ("Guitar2MIDI"))
        {
            portOut.open (d.identifier);
            return;
        }
}

void Guitar2MidiProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = params.copyState();
    state.setProperty ("midiPortId", portOut.getIdentifier(), nullptr);
    state.setProperty ("midiPortName", portOut.getName(), nullptr);
    state.setProperty ("userChosePort", userChosePort, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void Guitar2MidiProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (params.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    params.replaceState (state);

    const juce::String id = state.getProperty ("midiPortId");
    const juce::String name = state.getProperty ("midiPortName");
    const bool chose = state.getProperty ("userChosePort", false);

    // Port identifiers can change between sessions on Windows; fall back to the name.
    auto restore = [this, id, name, chose]
    {
        juce::String found;
        for (const auto& d : juce::MidiOutput::getAvailableDevices())
            if (d.identifier == id) { found = d.identifier; break; }
        if (found.isEmpty() && name.isNotEmpty())
            for (const auto& d : juce::MidiOutput::getAvailableDevices())
                if (d.name == name) { found = d.identifier; break; }

        if (found.isNotEmpty())
            selectPort (found);
        else if (chose && id.isEmpty())
            selectPort ({}); // user explicitly chose "none"
        userChosePort = chose;
    };

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        restore();
    else
        juce::MessageManager::callAsync ([safe = juce::WeakReference<Guitar2MidiProcessor> (this), restore]
                                         { if (safe != nullptr) restore(); });
}

juce::AudioProcessorEditor* Guitar2MidiProcessor::createEditor()
{
    return new Guitar2MidiEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Guitar2MidiProcessor();
}

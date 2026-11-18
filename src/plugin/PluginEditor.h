#pragma once

#include "PluginProcessor.h"

class Guitar2MidiEditor final : public juce::AudioProcessorEditor,
                                private juce::Timer
{
public:
    explicit Guitar2MidiEditor (Guitar2MidiProcessor&);
    ~Guitar2MidiEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshPorts();

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void addKnob (Knob& k, const juce::String& paramId, const juce::String& text, const juce::String& tooltip);

    Guitar2MidiProcessor& proc;
    juce::LookAndFeel_V4 lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::Label portLabel { {}, "MIDI out port:" };
    juce::ComboBox portBox;
    juce::TextButton refreshButton { "Refresh" };
    juce::Label statusLabel;
    juce::Array<juce::MidiDeviceInfo> ports;

    Knob gate, onset, tolerance, stability, release, lowest, highest, transpose, velSens, bendRange, channel;
    juce::ToggleButton bendToggle { "Pitch bend" }, passToggle { "Pass audio" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bendAttach, passAttach;

    // display state
    int shownNote = -1;
    float shownPitch = 0.0f, shownLevel = -120.0f;
    juce::Rectangle<int> displayArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Guitar2MidiEditor)
};

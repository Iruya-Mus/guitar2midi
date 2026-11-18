#include "PluginEditor.h"

namespace
{
    const juce::Colour bg        { 0xff1b1d22 };
    const juce::Colour panel     { 0xff262931 };
    const juce::Colour accent    { 0xff4fc3a1 };
    const juce::Colour textDim   { 0xff9aa0ab };
    const juce::Colour warn      { 0xffe0a040 };
}

Guitar2MidiEditor::Guitar2MidiEditor (Guitar2MidiProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    lnf.setColour (juce::Slider::rotarySliderFillColourId, accent);
    lnf.setColour (juce::Slider::thumbColourId, accent);
    lnf.setColour (juce::ToggleButton::tickColourId, accent);
    lnf.setColour (juce::ComboBox::backgroundColourId, panel);
    setLookAndFeel (&lnf);

    portLabel.setColour (juce::Label::textColourId, textDim);
    addAndMakeVisible (portLabel);
    addAndMakeVisible (portBox);
    addAndMakeVisible (refreshButton);
    statusLabel.setColour (juce::Label::textColourId, warn);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    portBox.onChange = [this]
    {
        const int idx = portBox.getSelectedId() - 2; // id 1 = "none"
        const juce::String id = juce::isPositiveAndBelow (idx, ports.size()) ? ports[idx].identifier : juce::String();
        if (! proc.selectPort (id))
            statusLabel.setText ("Could not open this port (is another program blocking it?)", juce::dontSendNotification);
        else
            statusLabel.setText ({}, juce::dontSendNotification);
    };
    refreshButton.onClick = [this] { refreshPorts(); };

    addKnob (gate,      "gate",      "Gate",       "Signal below this level is ignored. Raise it if noise triggers notes.");
    addKnob (onset,     "onset",     "Pick detect","How big a jump in level counts as a new pick. Lower = retrigger more easily.");
    addKnob (tolerance, "tolerance", "Tolerance",  "Pitch detection strictness. Lower = fewer wrong notes, higher = tracks weak notes.");
    addKnob (stability, "stability", "Stability",  "How many 4 ms frames a pitch must be steady before a note starts. Higher = fewer glitches, more latency.");
    addKnob (release,   "release",   "Release",    "How many 4 ms frames without pitch before the note is released.");
    addKnob (lowest,    "lowest",    "Lowest",     "Lowest note to detect. Set it to your lowest open string - higher values reduce latency.");
    addKnob (highest,   "highest",   "Highest",    "Highest note to detect.");
    addKnob (transpose, "transpose", "Transpose",  "Shift the MIDI output in semitones (12 = one octave).");
    addKnob (velSens,   "velsens",   "Dynamics",   "0 = every note has the same velocity, 1 = velocity follows how hard you pick.");
    addKnob (bendRange, "bendrange", "Bend range", "Must match the pitch bend range set in your synth.");
    addKnob (channel,   "channel",   "Channel",    "MIDI channel of the output.");

    for (auto* t : { &bendToggle, &passToggle })
    {
        t->setColour (juce::ToggleButton::textColourId, juce::Colours::white);
        addAndMakeVisible (*t);
    }
    bendToggle.setTooltip ("Send pitch bend so vibrato and string bends are reproduced.");
    passToggle.setTooltip ("Let the guitar sound through. Turn off to hear only the MIDI instrument.");
    bendAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, "bend", bendToggle);
    passAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, "passaudio", passToggle);

    refreshPorts();
    setSize (640, 430);
    startTimerHz (30);
}

Guitar2MidiEditor::~Guitar2MidiEditor()
{
    setLookAndFeel (nullptr);
}

void Guitar2MidiEditor::addKnob (Knob& k, const juce::String& paramId, const juce::String& text, const juce::String& tooltip)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    k.slider.setTooltip (tooltip);
    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, textDim);
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, paramId, k.slider);
}

void Guitar2MidiEditor::refreshPorts()
{
    // Pick up a port that was created after the plugin loaded.
    proc.autoSelectPort();

    ports = juce::MidiOutput::getAvailableDevices();
    portBox.clear (juce::dontSendNotification);
    portBox.addItem ("(none - host MIDI only)", 1);
    const auto current = proc.portOut.getIdentifier();
    int selected = 1;
    for (int i = 0; i < ports.size(); ++i)
    {
        portBox.addItem (ports[i].name, i + 2);
        if (ports[i].identifier == current)
            selected = i + 2;
    }
    portBox.setSelectedId (selected, juce::dontSendNotification);

    if (ports.isEmpty())
        statusLabel.setText ("No MIDI ports found. Install loopMIDI, create a port, then press Refresh.", juce::dontSendNotification);
    else if (selected == 1)
        statusLabel.setText ("Choose your loopMIDI port so Ableton can receive the notes.", juce::dontSendNotification);
    else
        statusLabel.setText ({}, juce::dontSendNotification);
}

void Guitar2MidiEditor::timerCallback()
{
    const int note = proc.engine.displayNote.load();
    const float pitch = proc.engine.displayPitch.load();
    const float level = proc.engine.displayLevelDb.load();
    if (note != shownNote || std::abs (pitch - shownPitch) > 0.01f || std::abs (level - shownLevel) > 0.3f)
    {
        shownNote = note;
        shownPitch = pitch;
        shownLevel = level;
        repaint (displayArea);
    }
}

void Guitar2MidiEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("Guitar2MIDI", 16, 10, 200, 28, juce::Justification::centredLeft);

    // --- display panel -----------------------------------------------------
    g.setColour (panel);
    g.fillRoundedRectangle (displayArea.toFloat(), 8.0f);
    auto area = displayArea.reduced (14, 10);

    // Note being sent
    auto noteArea = area.removeFromLeft (180);
    g.setColour (shownNote >= 0 ? accent : textDim.withAlpha (0.4f));
    g.setFont (juce::FontOptions (54.0f, juce::Font::bold));
    g.drawText (shownNote >= 0 ? Guitar2MidiProcessor::noteName (shownNote) : juce::String ("--"),
                noteArea.removeFromTop (70), juce::Justification::centred);
    g.setFont (juce::FontOptions (13.0f));
    g.setColour (textDim);
    g.drawText ("MIDI out", noteArea, juce::Justification::centredTop);

    area.removeFromLeft (14);

    // Tuner-style cents meter
    auto tuner = area.removeFromTop (46);
    g.setColour (textDim);
    g.setFont (juce::FontOptions (13.0f));
    if (shownPitch > 0.0f)
    {
        const int nearest = juce::roundToInt (shownPitch);
        const float cents = (shownPitch - (float) nearest) * 100.0f;
        g.drawText ("Detected: " + Guitar2MidiProcessor::noteName (nearest)
                        + juce::String::formatted ("  %+.0f cents", cents),
                    tuner.removeFromTop (20), juce::Justification::centredLeft);
        auto bar = tuner.reduced (0, 6).toFloat();
        g.setColour (bg);
        g.fillRoundedRectangle (bar, 3.0f);
        const float x = bar.getCentreX() + cents / 50.0f * bar.getWidth() * 0.5f;
        g.setColour (std::abs (cents) < 10.0f ? accent : warn);
        g.fillRect (juce::Rectangle<float> (x - 2.0f, bar.getY(), 4.0f, bar.getHeight()));
        g.setColour (textDim);
        g.drawVerticalLine ((int) bar.getCentreX(), bar.getY(), bar.getBottom());
    }
    else
    {
        g.drawText ("Detected: -", tuner.removeFromTop (20), juce::Justification::centredLeft);
    }

    // Level meter with gate marker
    area.removeFromTop (8);
    g.setColour (textDim);
    g.drawText ("Input level", area.removeFromTop (18), juce::Justification::centredLeft);
    auto meter = area.removeFromTop (14).toFloat();
    g.setColour (bg);
    g.fillRoundedRectangle (meter, 3.0f);
    auto toX = [&meter] (float db) { return meter.getX() + juce::jlimit (0.0f, 1.0f, (db + 80.0f) / 80.0f) * meter.getWidth(); };
    const float gateDb = proc.params.getRawParameterValue ("gate")->load();
    g.setColour (shownLevel > gateDb ? accent : textDim);
    g.fillRoundedRectangle (meter.withRight (toX (shownLevel)), 3.0f);
    g.setColour (warn);
    g.fillRect (juce::Rectangle<float> (toX (gateDb) - 1.0f, meter.getY() - 3.0f, 2.0f, meter.getHeight() + 6.0f));
}

void Guitar2MidiEditor::resized()
{
    auto r = getLocalBounds().reduced (16, 10);

    auto top = r.removeFromTop (28);
    top.removeFromLeft (190);
    refreshButton.setBounds (top.removeFromRight (70));
    top.removeFromRight (6);
    portBox.setBounds (top.removeFromRight (220));
    portLabel.setBounds (top);

    r.removeFromTop (4);
    statusLabel.setBounds (r.removeFromTop (20));
    r.removeFromTop (4);
    displayArea = r.removeFromTop (120);
    r.removeFromTop (12);

    auto layoutRow = [] (juce::Rectangle<int> row, std::initializer_list<Knob*> knobs)
    {
        const int w = row.getWidth() / (int) knobs.size();
        for (auto* k : knobs)
        {
            auto cell = row.removeFromLeft (w);
            k->label.setBounds (cell.removeFromTop (16));
            k->slider.setBounds (cell);
        }
    };

    const int rowH = (r.getHeight() - 8) / 2;
    layoutRow (r.removeFromTop (rowH), { &gate, &onset, &tolerance, &stability, &release, &velSens });
    r.removeFromTop (8);
    auto row2 = r.removeFromTop (rowH);
    auto toggles = row2.removeFromRight (row2.getWidth() / 6);
    bendToggle.setBounds (toggles.removeFromTop (toggles.getHeight() / 2).reduced (4, 8));
    passToggle.setBounds (toggles.reduced (4, 8));
    layoutRow (row2, { &lowest, &highest, &transpose, &bendRange, &channel });
}

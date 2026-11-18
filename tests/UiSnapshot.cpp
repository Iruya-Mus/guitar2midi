// Developer helper: renders the plugin window to a PNG (used for docs/screenshot.png).
#include "../src/plugin/PluginProcessor.h"
#include "../src/plugin/PluginEditor.h"
#include <cmath>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    Guitar2MidiProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 256);
    proc.prepareToPlay (48000.0, 256);

    // Feed a slightly sharp A3 so the display shows a note.
    juce::AudioBuffer<float> buf (2, 256);
    juce::MidiBuffer midi;
    double phase = 0.0;
    const double hz = 220.0 * std::pow (2.0, 8.0 / 1200.0);
    for (int b = 0; b < 60; ++b)
    {
        for (int i = 0; i < 256; ++i)
        {
            const float v = 0.3f * (float) (std::sin (phase) + 0.5 * std::sin (2 * phase) + 0.25 * std::sin (3 * phase));
            buf.setSample (0, i, v); buf.setSample (1, i, v);
            phase += 2.0 * juce::MathConstants<double>::pi * hz / 48000.0;
        }
        proc.processBlock (buf, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
    juce::File out { juce::String (argc > 1 ? argv[1] : "screenshot.png") };
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    ed.reset();
    return 0;
}

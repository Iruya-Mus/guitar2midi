// Loads the built VST3 the way a DAW does, feeds plucked-string audio
// and checks the MIDI that comes out. Also checks state save/restore.
// Usage: host_test <path to Guitar2MIDI.vst3>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cmath>
#include <cstdio>
#include <random>

static void pluck (juce::AudioBuffer<float>& buf, double sr, double start, double dur, int note, float amp)
{
    std::mt19937 rng ((unsigned) note);
    std::uniform_real_distribution<float> uni (-1.0f, 1.0f);
    const double hz = 440.0 * std::pow (2.0, (note - 69) / 12.0);
    const int L = (int) (sr / hz - 0.5);
    std::vector<float> line ((size_t) L);
    for (auto& v : line) v = uni (rng);
    float last = 0;
    const int s0 = (int) (start * sr), n = (int) (dur * sr);
    for (int i = 0; i < n && s0 + i < buf.getNumSamples(); ++i)
    {
        const size_t k = (size_t) (i % L);
        const float y = line[k];
        line[k] = 0.5f * (y + last) * 0.998f;
        last = y;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            buf.addSample (ch, s0 + i, 0.5f * amp * y);
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::printf ("usage: host_test <plugin.vst3>\n"); return 2; }

    juce::AudioPluginFormatManager fm;
    fm.addFormat (new juce::VST3PluginFormat());
    juce::OwnedArray<juce::PluginDescription> descs;
    juce::VST3PluginFormat vst3;
    vst3.findAllTypesForFile (descs, juce::String (argv[1]));
    if (descs.isEmpty()) { std::printf ("FAIL: plugin not found\n"); return 1; }

    const double sr = 48000.0;
    const int block = 256;
    juce::String err;
    auto plugin = fm.createPluginInstance (*descs[0], sr, block, err);
    if (plugin == nullptr) { std::printf ("FAIL: %s\n", err.toRawUTF8()); return 1; }
    std::printf ("loaded: %s  (produces MIDI: %d)\n", plugin->getName().toRawUTF8(), (int) plugin->producesMidi());

    plugin->setPlayConfigDetails (2, 2, sr, block);
    plugin->prepareToPlay (sr, block);

    const int notes[] = { 40, 47, 52, 57, 62, 67, 71, 76, 81 };
    juce::AudioBuffer<float> audio (2, (int) (sr * 6));
    audio.clear();
    double t = 0.2;
    for (int n : notes) { pluck (audio, sr, t, 0.45, n, 0.7f); t += 0.55; }

    std::vector<int> ons;
    int offs = 0;
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    for (int pos = 0; pos + block <= audio.getNumSamples(); pos += block)
    {
        for (int ch = 0; ch < 2; ++ch)
            buf.copyFrom (ch, 0, audio, ch, pos, block);
        midi.clear();
        plugin->processBlock (buf, midi);
        for (const auto m : midi)
        {
            const auto msg = m.getMessage();
            if (msg.isNoteOn()) ons.push_back (msg.getNoteNumber());
            else if (msg.isNoteOff()) ++offs;
        }
    }

    int failures = 0;
    std::printf ("note-ons:");
    for (int n : ons) std::printf (" %d", n);
    std::printf ("  (note-offs: %d)\n", offs);
    const bool notesOk = ons == std::vector<int> (std::begin (notes), std::end (notes)) && offs == (int) ons.size();
    std::printf ("MIDI from plugin: %s\n", notesOk ? "OK" : "FAIL");
    if (! notesOk) ++failures;

    // State round trip: change a parameter, save, change again, restore.
    auto* param = plugin->getParameters()[7]; // Transpose
    param->setValueNotifyingHost (0.75f);
    juce::MemoryBlock state;
    plugin->getStateInformation (state);
    param->setValueNotifyingHost (0.1f);
    plugin->setStateInformation (state.getData(), (int) state.getSize());
    const bool stateOk = std::abs (param->getValue() - 0.75f) < 0.02f;
    std::printf ("state restore (%s = %s): %s\n", param->getName (32).toRawUTF8(),
                 param->getCurrentValueAsText().toRawUTF8(), stateOk ? "OK" : "FAIL");
    if (! stateOk) ++failures;

    // Editor can be created and destroyed.
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (plugin->createEditorIfNeeded());
        std::printf ("editor: %s (%dx%d)\n", ed ? "OK" : "FAIL", ed ? ed->getWidth() : 0, ed ? ed->getHeight() : 0);
        if (! ed) ++failures;

        if (ed != nullptr && argc > 2) // optional: save a screenshot of the editor
        {
            // play a note so the display shows something
            juce::AudioBuffer<float> tone (2, (int) (sr * 0.3));
            tone.clear();
            pluck (tone, sr, 0.0, 0.3, 57, 0.6f);
            for (int pos = 0; pos + block <= tone.getNumSamples(); pos += block)
            {
                for (int ch = 0; ch < 2; ++ch) buf.copyFrom (ch, 0, tone, ch, pos, block);
                midi.clear();
                plugin->processBlock (buf, midi);
            }
            juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
            juce::File out { juce::String (argv[2]) };
            out.deleteFile();
            juce::FileOutputStream os (out);
            juce::PNGImageFormat().writeImageToStream (img, os);
            std::printf ("screenshot: %s\n", out.getFullPathName().toRawUTF8());
        }
    }

    plugin->releaseResources();
    plugin.reset();
    std::printf ("%s\n", failures == 0 ? "HOST TEST PASSED" : "HOST TEST FAILED");
    return failures == 0 ? 0 : 1;
}

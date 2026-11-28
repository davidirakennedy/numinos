// PluginProcessor — wraps whichever Numin is selected as a DAW plugin.
//
// One plugin, a menu to pick the Numin, and a
// fixed bank of kMaxParams generic parameters that take on the current
// Numin's names, values and display text.
#pragma once

#include "Registry.h"
#include "SamplePlayer.h"

#include <juce_audio_processors/juce_audio_processors.h>

class NuminosProcessor : public juce::AudioProcessor
{
  public:
    NuminosProcessor();

    // ---- Numin selection (message thread) ------------------------------
    // resetParams: true when the user picks from the menu, false when
    // restoring a saved session (keep the saved values).
    void setNumin(const juce::String& name, bool resetParams);

    struct NuminView // a snapshot of the current Numin, for the UI
    {
        juce::String name, category, blurb;
        juce::StringArray paramNames;
        juce::StringArray meterNames;                   // the Numin's meters (see Numin.h), if any
        juce::Array<numinos::Numin::MeterKind> meterKinds; // Trace, Line or Shade, per meter
        float meterLowDb = -80.0f, meterHighDb = 0.0f;
    };
    NuminView describeNumin() const;
    juce::String paramText(int index, float value) const;
    juce::String paramName(int index) const;
    float meterValue(int index) const { return meterValues[(size_t)index].load(); } // any thread

    juce::AudioProcessorValueTreeState params;
    bool isStandalone() const { return wrapperType == wrapperType_Standalone; }

    // ---- standalone "lab" ------------------------------------------------
    SamplePlayer player;
    juce::String loadSample(const juce::File& f);
    void saveLabSessionIfChanged(); // the editor calls this about once a second

    // ---- juce::AudioProcessor -------------------------------------------
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

  private:
    juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    bool copySidechain(juce::AudioBuffer<float>& buffer, int frames); // true if there is one (only reads it)
    void publishMeters(bool bypassed); // audio thread: copy the Numin's meters for the UI
    std::unique_ptr<juce::XmlElement> stateXml();
    void restoreFromXml(const juce::XmlElement& xml);
    static juce::File labSessionFile();

    std::unique_ptr<numinos::Numin> numin; // guarded by numinLock
    mutable juce::SpinLock numinLock;
    std::atomic<float>* paramValues[numinos::kMaxParams] = {};
    std::atomic<float>* bypassValue = nullptr;

    double sampleRate = 44100.0;
    std::vector<float> scratch; // right channel when running mono
    std::vector<float> sidechainL, sidechainR; // this block's sidechain, copied out of the host's buffer

    // A copy of the dry input, delayed by the current Numin's latency, so bypass (and the
    // brief pass-through while switching Numins) stays lined up with the processed sound.
    void writeDry(const juce::AudioBuffer<float>& buffer, int frames);
    void readDelayedDry(juce::AudioBuffer<float>& buffer, int frames);
    static constexpr double kMaxLatencySeconds = 4.0;
    juce::AudioBuffer<float> dryDelay;
    int dryWritePos = 0;
    std::atomic<int> latency{0};

    // The current Numin's meter values, copied on the audio thread after each block so the UI can
    // read them without ever taking numinLock (which would make the audio thread pass through dry).
    std::array<std::atomic<float>, numinos::Numin::kMaxMeters> meterValues{};

    juce::String lastSavedLabSession;

    JUCE_DECLARE_WEAK_REFERENCEABLE(NuminosProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NuminosProcessor)
};

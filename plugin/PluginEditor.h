// PluginEditor — a generic UI that builds itself from the current Numin: its name (click it to
// choose another Numin), its blurb, a live meter (if the Numin publishes meters), and one resin
// column per parameter; an A/B bypass button, an "About" page showing the Numin's README (if it
// has one), and (in the standalone lab) controls to loop an audio file through it.
// The look is NuminosLook's "Deep Resin"; nothing here is specific to any one Numin.
#pragma once

#include "MeterStrip.h"
#include "NuminPicker.h"
#include "NuminosLook.h"
#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

class NuminosEditor : public juce::AudioProcessorEditor,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer
{
  public:
    explicit NuminosEditor(NuminosProcessor&);
    ~NuminosEditor() override;

    void resized() override;
    void paint(juce::Graphics&) override;

    bool isInterestedInFileDrag(const juce::StringArray&) override { return processor.isStandalone(); }
    void filesDropped(const juce::StringArray& files, int, int) override;

  private:
    void timerCallback() override;
    void rebuildForNumin(); // rename/show/hide the columns for the current Numin, and resize
    void showAbout(bool show);
    void chooseSample();
    void refreshSampleLabel(const juce::String& error = {});
    void paintBackground(); // the stone, drawn once into `background` whenever the size changes
    int columnsPerRow() const;

    NuminosProcessor& processor;
    NuminosLook look; // declared before the components, so it outlives them
    juce::String shownNumin;
    int shownParams = 0;

    NuminTitle title{look};
    NuminList list{look};
    juce::Label blurb;
    std::array<juce::Slider, numinos::kMaxParams> columns;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, numinos::kMaxParams> attachments;
    juce::TextButton bypassButton{"A / B"};
    juce::TextButton aboutButton{"About"};
    juce::TextEditor aboutText; // the current Numin's README, read-only
    MeterStrip meter;           // the Numin's live meters, if it has any
    bool showingAbout = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    // standalone lab only
    juce::TextButton loadButton{"Load sample..."}, playButton{"Play"};
    juce::Label sampleLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Image background;     // the stone, without the live glow
    juce::Rectangle<int> glowArea; // where the meter's light spills onto the stone (set in resized())
    float glow = 0.0f;          // how far it spills right now, 0..1
    int ticks = 0;              // timer ticks, for the jobs that run less often

    // layout, top to bottom: header (wordmark + buttons), the Numin's name, its blurb, the lab row
    // (standalone only), the meter (if any), then the columns (or the About text in their place)
    static constexpr int width = 600, marginX = 28, marginY = 26, headerHeight = 38, titleHeight = 56,
                         blurbHeight = 26, gap = 18, labHeight = 36, meterHeight = 138,
                         columnGap = 14, columnHeight = 232, columnRowHeight = 170, maxColumnsPerRow = 6;
    static constexpr float kWarmth = 1.0f;        // the diffuse honey light on the stone (0: none)
    static constexpr float kBypassedLight = 0.4f; // how much light is left in the resin while bypassed

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NuminosEditor)
};

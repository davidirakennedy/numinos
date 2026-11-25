// PluginEditor — a generic UI that builds itself from the current Numin:
// a menu to pick the Numin, one slider per parameter, an A/B bypass button,
// an "About" page showing the Numin's README (if it has one), a live meter
// strip (if the Numin publishes meters) with a gain needle beside it (if it
// publishes a Gain), and (in the
// standalone lab) controls to loop an audio file through it.
#pragma once

#include "GainNeedle.h"
#include "MeterStrip.h"
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
    void rebuildForNumin(); // relabel/show/hide sliders for the current Numin
    void showAbout(bool show);
    void chooseSample();
    void refreshSampleLabel(const juce::String& error = {});

    struct Row
    {
        juce::Label label;
        juce::Slider slider{juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight};
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    NuminosProcessor& processor;
    NuminosLook look; // declared before the components, so it outlives them
    juce::String shownNumin;

    juce::ComboBox numinMenu;
    juce::Label blurb;
    std::array<Row, numinos::kMaxParams> rows;
    juce::TextButton bypassButton{"A/B: Bypass"};
    juce::TextButton aboutButton{"About"};
    juce::TextEditor aboutText; // the current Numin's README, read-only
    MeterStrip meter;           // the Numin's live meters, if it has any
    GainNeedle needle;          // and its gain, as a needle beside them
    bool showingAbout = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    // standalone lab only
    juce::TextButton loadButton{"Load sample..."}, playButton{"Play"};
    juce::Label sampleLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Image grain;      // paper texture, made once in the constructor
    int ruleY = 0;          // where paint() draws the line under the Numin's name (set in resized())

    // layout, top to bottom: header (wordmark + buttons), the Numin's name, its blurb,
    // a rule, the lab row (standalone only), the meter strip (if any), then the sliders or the About text
    static constexpr int width = 560, margin = 22, headerHeight = 26, titleHeight = 40,
                         blurbHeight = 24, gap = 14, rowHeight = 36, labelWidth = 120,
                         valueWidth = 120, aboutHeight = 420, meterHeight = 110, needleWidth = 150;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NuminosEditor)
};

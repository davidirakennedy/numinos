// PluginEditor — a generic UI that builds itself from the current Numin:
// a menu to pick the Numin, one slider per parameter, an A/B bypass button,
// an "About" page showing the Numin's README (if it has one), and (in the
// standalone lab) controls to loop an audio file through it.
#pragma once

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
    juce::String shownNumin;

    juce::ComboBox numinMenu;
    juce::Label blurb;
    std::array<Row, numinos::kMaxParams> rows;
    juce::TextButton bypassButton{"A/B: Bypass"};
    juce::TextButton aboutButton{"About"};
    juce::TextEditor aboutText; // the current Numin's README, read-only
    bool showingAbout = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    // standalone lab only
    juce::TextButton loadButton{"Load sample..."}, playButton{"Play"};
    juce::Label sampleLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    static constexpr int rowHeight = 30, margin = 12, aboutHeight = 420;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NuminosEditor)
};

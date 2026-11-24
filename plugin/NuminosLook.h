// NuminosLook — the plugin's visual style, borrowed from numinos.art: cool drawing
// paper, graphite ink, IM Fell small caps for names and EB Garamond for everything
// else, with the honey drop (the site's favicon) as the slider thumb.
//
// Every colour lives in NuminosLook::colours below, mirroring :root in the site's
// CSS, so a palette change is one edit in one place.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class NuminosLook : public juce::LookAndFeel_V4
{
  public:
    struct Colours
    {
        // same names and values as the site's CSS variables
        static inline const juce::Colour paper{0xffecece8}, paperDeep{0xffdfdfdb}, ink{0xff222326},
            graphite{0xff6c6e72}, faint{0xffa6a8ab}, indigo{0xff4a5363}, amber{0xff9a6a2c};
        static inline const juce::Colour hairline = ink.withAlpha(0.18f); // the site's rule lines
    };

    NuminosLook();

    // The three faces the site uses, at a given height in pixels.
    juce::Font serif(float height) const;
    juce::Font italic(float height) const;
    juce::Font caps(float height, float letterSpacing = 0.08f) const;

    // The honey drop, scaled to fit `area` (point up, belly down).
    static juce::Path dropShape(juce::Rectangle<float> area);

    // ---- LookAndFeel overrides ------------------------------------------------
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                          float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle,
                          juce::Slider&) override;
    int getSliderThumbRadius(juce::Slider&) override { return 9; }
    juce::Label* createSliderTextBox(juce::Slider&) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted,
                              bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;

    void drawComboBox(juce::Graphics&, int width, int height, bool down, int, int, int, int,
                      juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;

    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuSectionHeader(juce::Graphics&, const juce::Rectangle<int>&,
                                    const juce::String&) override;

    void drawTextEditorOutline(juce::Graphics&, int, int, juce::TextEditor&) override {} // no box

  private:
    juce::Typeface::Ptr serifFace, italicFace, capsFace;
};

// NuminosLook — the plugin's visual style, "Deep Resin": dark obsidian, with honey-coloured
// resin as the only real light. The meter is a block of resin with the signal glowing gold
// inside it; each control is a column of resin that fills with light as you turn it up. Both
// sit in wells cut into the stone. IM Fell small caps for names, EB Garamond for everything
// else, and the honey drop (numinos.art's favicon) as the mark.
//
// Every colour lives in NuminosLook::Colours below, so a palette change is one edit in one
// place.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class NuminosLook : public juce::LookAndFeel_V4
{
  public:
    struct Colours
    {
        // the stone
        static inline const juce::Colour obsidian{0xff040405},   // the body of the window, and the wells
            obsidianLit{0xff0c0c0e},                               // where the faint light falls (top left)
            obsidianEdge{0xff010101};                              // the corners
        // the ink: soft grey-whites, never pure white
        static inline const juce::Colour title{0xffe4e0d8}, text{0xffdcd8d0}, label{0xff9c9a95},
            muted{0xff8e8f95}, wordmark{0xff8a8b91}, faint{0xff5f6065};
        // the resin and its light
        static inline const juce::Colour honey{0xffffb75c},       // the drop, focus rings, "on"
            gold{0xffffd08f},                                      // the brightest light in the resin
            amber{0xffff9a2e},                                     // where the light fades
            surface{0xfffff1d8},                                   // the line where the light ends
            resinTop{0x26ffaa46}, resinBottom{0x66692e08};         // the resin itself, top to bottom
    };

    NuminosLook();

    // The three faces, at a given height in pixels.
    juce::Font serif(float height) const;
    juce::Font italic(float height) const;
    juce::Font caps(float height, float letterSpacing = 0.08f) const;

    // The honey drop, scaled to fit `area` (point up, belly down).
    static juce::Path dropShape(juce::Rectangle<float> area);

    // A well cut into the stone: a dark recess with its top edge in shadow and a faint lit lip
    // along the bottom. Everything lit sits in one.
    static void drawWell(juce::Graphics&, juce::Rectangle<float> area, float radius);

    // How bright the resin is, 0..1. The editor lowers it while the plugin is bypassed, so the
    // light drains out of the window when you hear the dry sound.
    float light = 1.0f;

    // ---- LookAndFeel overrides ------------------------------------------------
    // The controls are bar sliders (LinearBarVertical) drawn as resin columns.
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                          float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle,
                          juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout(juce::Slider&) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted,
                              bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;

    void drawTextEditorOutline(juce::Graphics&, int, int, juce::TextEditor&) override {} // no box

    // where a column's name and value sit, from its top edge
    static constexpr int kColumnInset = 4, kColumnNameTop = 14, kColumnValueTop = 30, kColumnValueHeight = 22;

  private:
    juce::Typeface::Ptr serifFace, italicFace, capsFace;
};

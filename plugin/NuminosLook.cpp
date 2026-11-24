#include "NuminosLook.h"

#include "NuminosFonts.h" // the .ttf files in plugin/fonts, embedded at build time

using C = NuminosLook::Colours;

namespace
{
juce::Typeface::Ptr loadFace(const char* data, int size)
{
    return juce::Typeface::createSystemTypefaceFor(data, (size_t)size);
}
} // namespace

NuminosLook::NuminosLook()
{
    serifFace = loadFace(NuminosFonts::EBGaramondRegular_ttf, NuminosFonts::EBGaramondRegular_ttfSize);
    italicFace = loadFace(NuminosFonts::EBGaramondItalic_ttf, NuminosFonts::EBGaramondItalic_ttfSize);
    capsFace = loadFace(NuminosFonts::IMFellEnglishSCRegular_ttf, NuminosFonts::IMFellEnglishSCRegular_ttfSize);
    setDefaultSansSerifTypeface(serifFace); // anything not styled below is set in Garamond

    // LookAndFeel_V4 draws from a colour table; point the parts we don't redraw
    // ourselves at the site's palette.
    setColour(juce::ResizableWindow::backgroundColourId, C::paper);
    setColour(juce::Label::textColourId, C::ink);

    setColour(juce::TextButton::buttonColourId, C::paper);
    setColour(juce::TextButton::buttonOnColourId, C::paperDeep);
    setColour(juce::TextButton::textColourOffId, C::graphite);
    setColour(juce::TextButton::textColourOnId, C::amber);

    setColour(juce::Slider::textBoxTextColourId, C::ink);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, C::amber.withAlpha(0.25f));

    setColour(juce::ComboBox::textColourId, C::ink);
    setColour(juce::ComboBox::arrowColourId, C::graphite);
    setColour(juce::PopupMenu::backgroundColourId, C::paper);
    setColour(juce::PopupMenu::textColourId, C::ink);
    setColour(juce::PopupMenu::headerTextColourId, C::indigo);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, C::paperDeep);
    setColour(juce::PopupMenu::highlightedTextColourId, C::indigo);

    setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::textColourId, C::ink);
    setColour(juce::TextEditor::highlightColourId, C::amber.withAlpha(0.25f));
    setColour(juce::TextEditor::highlightedTextColourId, C::ink);
    setColour(juce::CaretComponent::caretColourId, C::ink);
    setColour(juce::ScrollBar::thumbColourId, C::faint);
}

juce::Font NuminosLook::serif(float height) const
{
    return juce::Font(juce::FontOptions(serifFace).withHeight(height));
}

juce::Font NuminosLook::italic(float height) const
{
    return juce::Font(juce::FontOptions(italicFace).withHeight(height));
}

juce::Font NuminosLook::caps(float height, float letterSpacing) const
{
    // letterSpacing is a fraction of the font height, like CSS's `em`
    return juce::Font(juce::FontOptions(capsFace).withHeight(height).withKerningFactor(letterSpacing));
}

juce::Path NuminosLook::dropShape(juce::Rectangle<float> area)
{
    // The drop from numinos.art's favicon.svg, path copied as-is, then scaled into place.
    static const juce::Path drop =
        juce::Drawable::parseSVGPath("M32 38 C35 43, 38 46, 38 50 A6 6 0 0 1 26 50 C26 46, 29 43, 32 38 Z");
    auto p = drop;
    p.applyTransform(p.getTransformToScaleToFit(area, true));
    return p;
}

// ---- sliders: a hairline with the drop resting on it ----------------------------

void NuminosLook::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                   float, float, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal)
        return LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, 0, 0, style, slider);

    const float midY = (float)y + (float)height * 0.5f + 3.0f; // a little low, so the drop sits on the line
    const float left = (float)x, right = (float)(x + width);

    g.setColour(C::hairline);
    g.fillRect(juce::Rectangle<float>(left, midY - 0.5f, right - left, 1.0f));
    g.setColour(C::ink.withAlpha(slider.isEnabled() ? 0.7f : 0.3f)); // the amount so far, in ink
    g.fillRect(juce::Rectangle<float>(left, midY - 0.75f, sliderPos - left, 1.5f));

    // The drop: its belly centred on the line at the value, point up.
    const float dropH = 17.0f, dropW = dropH * (12.0f / 18.0f); // the favicon drop is 12 x 18
    auto area = juce::Rectangle<float>(dropW, dropH).withCentre({sliderPos, midY - dropH * 0.5f + dropW * 0.5f});
    const bool active = slider.isMouseOverOrDragging();
    g.setColour(active ? C::amber.darker(0.25f) : C::amber);
    g.fillPath(dropShape(area));
}

juce::Label* NuminosLook::createSliderTextBox(juce::Slider& slider)
{
    auto* box = LookAndFeel_V4::createSliderTextBox(slider);
    box->setFont(italic(17.0f));
    box->setJustificationType(juce::Justification::centredRight);
    return box;
}

// ---- buttons: small caps in a pill, like the site's "close" ------------------------

void NuminosLook::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                       bool highlighted, bool down)
{
    auto r = button.getLocalBounds().toFloat().reduced(0.5f);
    const float radius = r.getHeight() * 0.5f;

    if (button.getToggleState() || down)
        g.setColour(C::paperDeep);
    else
        g.setColour(highlighted ? C::paperDeep.withAlpha(0.5f) : C::paper.withAlpha(0.0f));
    g.fillRoundedRectangle(r, radius);

    g.setColour(button.getToggleState() ? C::amber.withAlpha(0.6f) : C::hairline);
    g.drawRoundedRectangle(r, radius, 1.0f);
}

juce::Font NuminosLook::getTextButtonFont(juce::TextButton&, int)
{
    return caps(14.0f, 0.12f);
}

// ---- the Numin menu: the Numin's name as a title, with a quiet chevron ------------

void NuminosLook::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int,
                               juce::ComboBox& box)
{
    const float h = (float)height, w = (float)width;
    g.setColour(box.isMouseOver(true) ? C::indigo : C::graphite);
    juce::Path chevron;
    const float cx = w - 10.0f, cy = h * 0.5f;
    chevron.startNewSubPath(cx - 5.0f, cy - 2.5f);
    chevron.lineTo(cx, cy + 2.5f);
    chevron.lineTo(cx + 5.0f, cy - 2.5f);
    g.strokePath(chevron, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font NuminosLook::getComboBoxFont(juce::ComboBox&)
{
    return caps(26.0f, 0.04f);
}

void NuminosLook::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(0, 0, box.getWidth() - 24, box.getHeight());
    label.setBorderSize({});
    label.setFont(getComboBoxFont(box));
}

// ---- the menu that drops down --------------------------------------------------

void NuminosLook::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    g.fillAll(C::paper);
    g.setColour(C::hairline);
    g.drawRect(0, 0, width, height, 1);
}

juce::Font NuminosLook::getPopupMenuFont()
{
    return serif(18.0f);
}

void NuminosLook::drawPopupMenuSectionHeader(juce::Graphics& g, const juce::Rectangle<int>& area,
                                             const juce::String& name)
{
    g.setFont(caps(13.0f, 0.14f));
    g.setColour(C::indigo);
    g.drawFittedText(name, area.reduced(12, 0).withTrimmedTop(4), juce::Justification::centredLeft, 1);
}

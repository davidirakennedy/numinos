#include "NuminosLook.h"

#include "NuminosFonts.h" // the .ttf files in plugin/fonts, embedded at build time

using C = NuminosLook::Colours;

namespace
{
juce::Typeface::Ptr loadFace(const char* data, int size)
{
    return juce::Typeface::createSystemTypefaceFor(data, (size_t)size);
}

// A vertical gradient filling `area`, from `top` to `bottom`.
void fillVertical(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour top, juce::Colour bottom)
{
    g.setGradientFill(juce::ColourGradient(top, area.getTopLeft(), bottom, area.getBottomLeft(), false));
    g.fillRect(area);
}
} // namespace

NuminosLook::NuminosLook()
{
    serifFace = loadFace(NuminosFonts::EBGaramondRegular_ttf, NuminosFonts::EBGaramondRegular_ttfSize);
    italicFace = loadFace(NuminosFonts::EBGaramondItalic_ttf, NuminosFonts::EBGaramondItalic_ttfSize);
    capsFace = loadFace(NuminosFonts::IMFellEnglishSCRegular_ttf, NuminosFonts::IMFellEnglishSCRegular_ttfSize);
    setDefaultSansSerifTypeface(serifFace); // anything not styled below is set in Garamond

    // LookAndFeel_V4 draws from a colour table; point the parts we don't redraw ourselves at the
    // palette (the About text, its scrollbar, the right-click menu on it).
    setColour(juce::ResizableWindow::backgroundColourId, C::obsidian);
    setColour(juce::Label::textColourId, C::text);

    setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    setColour(juce::TextButton::textColourOffId, juce::Colour(0xffcfcbc3));
    setColour(juce::TextButton::textColourOnId, C::honey);

    setColour(juce::Slider::textBoxTextColourId, C::title);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, C::honey.withAlpha(0.25f));

    setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::textColourId, C::text);
    setColour(juce::TextEditor::highlightColourId, C::honey.withAlpha(0.25f));
    setColour(juce::TextEditor::highlightedTextColourId, C::title);
    setColour(juce::CaretComponent::caretColourId, C::honey);
    setColour(juce::ScrollBar::thumbColourId, C::faint);

    setColour(juce::PopupMenu::backgroundColourId, C::obsidianLit);
    setColour(juce::PopupMenu::textColourId, C::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, C::honey.withAlpha(0.15f));
    setColour(juce::PopupMenu::highlightedTextColourId, C::title);
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

void NuminosLook::drawWell(juce::Graphics& g, juce::Rectangle<float> area, float radius)
{
    // the lit lip: the same shape one pixel lower, mostly covered by the well itself
    g.setColour(juce::Colours::white.withAlpha(0.045f));
    g.drawRoundedRectangle(area.translated(0.0f, 1.0f).reduced(0.5f), radius, 1.0f);

    juce::Path shape;
    shape.addRoundedRectangle(area, radius);
    g.setColour(C::obsidian);
    g.fillPath(shape);

    // the shadow just inside the top edge, where the light can't reach
    g.saveState();
    g.reduceClipRegion(shape);
    fillVertical(g, area.withHeight(9.0f), juce::Colours::black.withAlpha(0.9f), juce::Colours::transparentBlack);
    g.restoreState();
}

// ---- controls: columns of resin, lit from the bottom up to the value ---------------------

juce::Slider::SliderLayout NuminosLook::getSliderLayout(juce::Slider& slider)
{
    if (slider.getSliderStyle() != juce::Slider::LinearBarVertical)
        return LookAndFeel_V4::getSliderLayout(slider);

    // The light fills the column inside the well; the value sits near its top, under the name
    // (drawn in drawLinearSlider). Click the value to type one; drag anywhere else.
    const auto bounds = slider.getLocalBounds();
    juce::Slider::SliderLayout layout;
    layout.sliderBounds = bounds.reduced(kColumnInset);
    layout.textBoxBounds = juce::Rectangle<int>(bounds.getX() + kColumnInset + 4, bounds.getY() + kColumnInset + kColumnValueTop,
                                                bounds.getWidth() - 2 * (kColumnInset + 4), kColumnValueHeight);
    return layout;
}

void NuminosLook::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                   float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style,
                                   juce::Slider& slider)
{
    if (style != juce::Slider::LinearBarVertical)
        return LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos,
                                                style, slider);

    const auto well = slider.getLocalBounds().toFloat();
    const auto column = juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height);
    const float radius = 13.0f;
    const bool active = slider.isMouseOverOrDragging();
    drawWell(g, well, radius + (float)kColumnInset);

    juce::Path shape;
    shape.addRoundedRectangle(column, radius);
    g.saveState();
    g.reduceClipRegion(shape);

    // the resin, barely there until it's lit
    fillVertical(g, column, juce::Colour(0x06ffb75c), juce::Colour(0x12ff8c28));

    // the light, from the bottom up to the value, with a soft glow rising off its surface
    const float top = juce::jlimit(column.getY(), column.getBottom(), sliderPos);
    const float bright = (active ? 1.0f : 0.85f) * light;
    fillVertical(g, column.withTop(top), juce::Colour(0xffffc474).withAlpha(0.6f * bright),
                 juce::Colour(0xffff8c28).withAlpha(0.22f * bright));
    fillVertical(g, column.withTop(top - 26.0f).withBottom(top), juce::Colours::transparentBlack,
                 juce::Colour(0xffffa03c).withAlpha(0.28f * light));

    // the surface: a line of light from wall to wall, softer toward the walls, with a halo
    juce::ColourGradient halo(juce::Colours::transparentBlack, column.getX(), top - 6.0f,
                              juce::Colours::transparentBlack, column.getX(), top + 6.0f, false);
    halo.addColour(0.5, juce::Colour(0xffffc070).withAlpha(0.4f * light));
    g.setGradientFill(halo);
    g.fillRect(column.withTop(top - 6.0f).withHeight(12.0f));
    juce::ColourGradient line(C::surface.withAlpha(0.55f * light), column.getX(), top,
                              C::surface.withAlpha(0.55f * light), column.getRight(), top, false);
    line.addColour(0.2, C::surface.withAlpha(light));
    line.addColour(0.8, C::surface.withAlpha(light));
    g.setGradientFill(line);
    g.fillRect(column.getX(), top - 0.75f, column.getWidth(), 1.5f);

    // an optional mark (a Numin's ParamInfo::mark), e.g. where its musical range ends: a short
    // tick on each wall
    const float mark = (float)(double)slider.getProperties().getWithDefault("mark", -1.0);
    if (mark >= 0.0f)
    {
        const float markY = (float)slider.getPositionOfValue(mark);
        g.setColour(juce::Colour(0x99ffe6be));
        g.fillRect(column.getX(), markY, 9.0f, 1.0f);
        g.fillRect(column.getRight() - 9.0f, markY, 9.0f, 1.0f);
    }

    // shadow falling into the top of the column, and a faint sheen across the glass
    fillVertical(g, column.withHeight(64.0f), juce::Colours::black.withAlpha(0.55f), juce::Colours::transparentBlack);
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.07f), column.getTopLeft(),
                                           juce::Colours::transparentBlack,
                                           column.getTopLeft() + juce::Point<float>(column.getWidth(), column.getHeight()) * 0.26f,
                                           false));
    g.fillRect(column);
    g.restoreState();

    // the rim, and a honey ring when it has keyboard focus
    g.setColour(C::honey.withAlpha(active ? 0.28f : 0.16f));
    g.drawRoundedRectangle(column.reduced(0.5f), radius, 1.0f);
    if (slider.hasKeyboardFocus(true))
    {
        g.setColour(C::honey);
        g.drawRoundedRectangle(well.reduced(0.5f), radius + (float)kColumnInset, 1.5f);
    }

    // the name, at the top; the value is the slider's text box just below it
    g.setColour(C::label);
    g.setFont(caps(13.0f, 0.14f));
    g.drawFittedText(slider.getName(), well.toNearestInt().reduced(kColumnInset + 4, 0)
                                           .withY((int)well.getY() + kColumnInset + kColumnNameTop - 3)
                                           .withHeight(16),
                     juce::Justification::centred, 1, 0.7f);
}

juce::Label* NuminosLook::createSliderTextBox(juce::Slider& slider)
{
    auto* box = LookAndFeel_V4::createSliderTextBox(slider);
    box->setFont(italic(17.0f));
    box->setJustificationType(juce::Justification::centred);
    box->setMinimumHorizontalScale(0.7f);
    return box;
}

// ---- buttons: small caps in a softly rounded pane of stone; when on, lit from within --------

void NuminosLook::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                       bool highlighted, bool down)
{
    auto r = button.getLocalBounds().toFloat().reduced(0.5f);
    const float radius = 9.0f;
    const bool on = button.getToggleState();

    if (on)
    {
        // on: a small pane of resin with light rising in it, like the columns
        g.setGradientFill(juce::ColourGradient(C::honey.withAlpha(0.05f), r.getX(), r.getY(),
                                               C::honey.withAlpha(0.22f), r.getX(), r.getBottom(), false)); // not dimmed by `light`: A/B stays lit while bypassed
        g.fillRoundedRectangle(r, radius);
        g.setColour(C::honey.withAlpha(highlighted ? 0.4f : 0.28f));
    }
    else
    {
        g.setColour(juce::Colours::white.withAlpha(highlighted ? 0.035f : 0.02f));
        g.fillRoundedRectangle(r, radius);
        g.setColour(juce::Colours::white.withAlpha(highlighted ? 0.18f : 0.10f));
    }
    g.drawRoundedRectangle(r, radius, 1.0f);

    // while held down, it's pressed a little into the stone
    if (down)
    {
        g.setColour(juce::Colours::black.withAlpha(0.3f));
        g.fillRoundedRectangle(r, radius);
    }

}

juce::Font NuminosLook::getTextButtonFont(juce::TextButton&, int)
{
    return caps(14.0f, 0.12f);
}

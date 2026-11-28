#include "PluginEditor.h"

#if NUMINOS_HAS_DOCS
    #include "BinaryData.h"
#endif

namespace
{
// The README embedded for a Numin (numins/<Name>/README.md, saved as "<Name>.md"), as plain
// text: markdown heading marks are dropped, everything else is shown as written.
juce::String readmeFor(const juce::String& numinName)
{
#if NUMINOS_HAS_DOCS
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        if (juce::String(BinaryData::originalFilenames[i]) != numinName + ".md")
            continue;
        int size = 0;
        const char* data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size);
        juce::StringArray lines;
        lines.addLines(juce::String::fromUTF8(data, size));
        for (auto& line : lines)
            if (line.startsWith("#"))
                line = line.trimCharactersAtStart("#").trimStart();
        return lines.joinIntoString("\n").trim();
    }
#else
    juce::ignoreUnused(numinName);
#endif
    return {};
}

// A radial glow: `colour` at `centre`, fading to nothing at `radius`.
juce::ColourGradient glowAt(juce::Point<float> centre, float radius, juce::Colour colour)
{
    return juce::ColourGradient(colour, centre, colour.withAlpha(0.0f), centre.translated(radius, 0.0f), true);
}
} // namespace

NuminosEditor::NuminosEditor(NuminosProcessor& p)
    : AudioProcessorEditor(p), processor(p), meter(look, [this](int i) { return processor.meterValue(i); })
{
    // ---- the name, and the list that drops from it ---------------------------------
    title.onClick = [this] {
        title.setOpen(true);
        list.show(shownNumin, {title.getX() - 8, title.getBottom() + 4});
    };
    list.onPick = [this](const juce::String& name) {
        if (name != shownNumin)
        {
            processor.setNumin(name, true);
            rebuildForNumin();
        }
    };
    list.onClose = [this] { title.setOpen(false); };
    addAndMakeVisible(title);

    blurb.setColour(juce::Label::textColourId, NuminosLook::Colours::muted);
    blurb.setFont(look.italic(18.0f));
    blurb.setBorderSize({});
    blurb.setMinimumHorizontalScale(0.85f);
    addAndMakeVisible(blurb);

    // ---- one resin column per generic parameter --------------------------------------
    // Drag up or down from anywhere on a column (it doesn't jump to the click); hold Shift for
    // finer moves; double-click to reset; click the value to type one.
    for (int i = 0; i < numinos::kMaxParams; ++i)
    {
        auto& column = columns[(size_t)i];
        column.setSliderStyle(juce::Slider::LinearBarVertical);
        column.setTextBoxStyle(juce::Slider::TextBoxAbove, false, 100, NuminosLook::kColumnValueHeight);
        column.setSliderSnapsToMousePosition(false);
        column.setVelocityModeParameters(0.4, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
        column.setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        column.setWantsKeyboardFocus(true);
        attachments[(size_t)i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.params, "p" + juce::String(i + 1), column);
        column.setDoubleClickReturnValue(true, 0.5); // updated per Numin below
        addChildComponent(column);
    }

    bypassButton.setClickingTogglesState(true);
    bypassButton.setTooltip("Bypass: hear the dry sound");
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.params, "bypass", bypassButton);
    addAndMakeVisible(bypassButton);

    aboutButton.setClickingTogglesState(true);
    aboutButton.onClick = [this] { showAbout(aboutButton.getToggleState()); };
    addChildComponent(aboutButton); // shown only for Numins that have a README
    aboutText.setMultiLine(true, true);
    aboutText.setReadOnly(true);
    aboutText.setCaretVisible(false);
    aboutText.setScrollbarsShown(true);
    aboutText.setFont(look.serif(17.0f));
    aboutText.setScrollBarThickness(6);
    addChildComponent(aboutText);
    addChildComponent(meter);

    // ---- standalone lab: loop a sample through the Numin ---------------------
    if (processor.isStandalone())
    {
        loadButton.onClick = [this] { chooseSample(); };
        playButton.setClickingTogglesState(true);
        playButton.onClick = [this] { processor.player.setPlaying(playButton.getToggleState()); };
        sampleLabel.setColour(juce::Label::textColourId, NuminosLook::Colours::muted);
        sampleLabel.setFont(look.italic(16.0f));
        addAndMakeVisible(loadButton);
        addAndMakeVisible(playButton);
        addAndMakeVisible(sampleLabel);
        refreshSampleLabel();
    }

    addChildComponent(list); // last, so it lies over everything when it's open

    // Clicking a control doesn't give it keyboard focus, so focus rings only appear when you
    // move around with Tab, not after every click.
    for (auto* child : getChildren())
        child->setMouseClickGrabsKeyboardFocus(false);
    for (auto* button : {&bypassButton, &aboutButton, &loadButton, &playButton})
        button->setWantsKeyboardFocus(false); // simple toggles: no focus, so never a stray focus ring

    setLookAndFeel(&look); // set last, so every child above is restyled (sliders rebuild their value boxes)
    look.light = bypassButton.getToggleState() ? kBypassedLight : 1.0f;
    rebuildForNumin();
    startTimerHz(30);
}

NuminosEditor::~NuminosEditor()
{
    setLookAndFeel(nullptr);
    if (processor.isStandalone())
        processor.saveLabSessionIfChanged();
}

int NuminosEditor::columnsPerRow() const
{
    // up to six in one row; more go in two rows, split evenly
    return shownParams <= maxColumnsPerRow ? shownParams : (shownParams + 1) / 2;
}

void NuminosEditor::rebuildForNumin()
{
    const auto view = processor.describeNumin();
    shownNumin = view.name;
    shownParams = view.paramNames.size();
    list.close();

    title.setNuminName(view.name);
    blurb.setText(view.blurb, juce::dontSendNotification);

    const auto readme = readmeFor(view.name);
    aboutText.setText(readme, false);
    aboutButton.setVisible(readme.isNotEmpty());
    if (readme.isEmpty())
        showingAbout = false;
    aboutButton.setToggleState(showingAbout, juce::dontSendNotification);
    aboutText.setVisible(showingAbout);
    meter.configure(view);
    meter.setVisible(meter.hasMeters() && !showingAbout);

    auto numin = numinos::createNumin(view.name.toStdString()); // for default values and marks
    for (int i = 0; i < numinos::kMaxParams; ++i)
    {
        auto& column = columns[(size_t)i];
        const bool used = i < shownParams;
        column.setVisible(used && !showingAbout);
        if (used)
        {
            column.setName(view.paramNames[i]); // drawn at the top of the column by NuminosLook
            column.setTitle(view.paramNames[i]); // for screen readers
            column.setDoubleClickReturnValue(true, numin->paramInfo(i).defaultValue);
            column.getProperties().set("mark", numin->paramInfo(i).mark); // drawn by NuminosLook, if >= 0
            column.updateText(); // display text depends on the Numin
        }
    }

    // The window is the same height with or without the About text, so it doesn't jump.
    const int lab = processor.isStandalone() ? labHeight + gap : 0;
    const int metering = meter.hasMeters() ? meterHeight + gap : 0;
    const int controls = shownParams == 0 ? 0 : shownParams <= maxColumnsPerRow ? columnHeight : 2 * columnRowHeight + columnGap;
    setSize(width, marginY + headerHeight + gap + titleHeight + blurbHeight + gap + lab + metering + controls + marginY);
    resized();
    repaint();
}

void NuminosEditor::showAbout(bool show)
{
    showingAbout = show;
    rebuildForNumin(); // swaps the meter and columns for the text (or back)
}

void NuminosEditor::resized()
{
    auto area = getLocalBounds().reduced(marginX, marginY);

    // header: the wordmark is part of the background (paintBackground()); buttons sit on the right
    auto header = area.removeFromTop(headerHeight);
    bypassButton.setBounds(header.removeFromRight(84));
    if (aboutButton.isVisible())
    {
        header.removeFromRight(10);
        aboutButton.setBounds(header.removeFromRight(92));
    }
    area.removeFromTop(gap);

    auto titleRow = area.removeFromTop(titleHeight);
    title.setBounds(titleRow.withWidth(juce::jmin(titleRow.getWidth(), title.idealWidth())));
    blurb.setBounds(area.removeFromTop(blurbHeight));
    area.removeFromTop(gap);

    if (processor.isStandalone())
    {
        auto labRow = area.removeFromTop(labHeight);
        loadButton.setBounds(labRow.removeFromLeft(150));
        labRow.removeFromLeft(10);
        playButton.setBounds(labRow.removeFromLeft(80));
        labRow.removeFromLeft(14);
        sampleLabel.setBounds(labRow);
        area.removeFromTop(gap);
    }

    list.setBounds(getLocalBounds());
    glowArea = {};

    if (showingAbout)
    {
        aboutText.setBounds(area.reduced(18, 14)); // inside a well, drawn in paint()
    }
    else
    {
        if (meter.isVisible())
        {
            meter.setBounds(area.removeFromTop(meterHeight));
            glowArea = meter.getBounds().expanded(30, 70);
            area.removeFromTop(gap);
        }

        const int perRow = columnsPerRow();
        const int rows = shownParams > perRow ? 2 : 1;
        const int rowHeight = rows == 1 ? area.getHeight() : (area.getHeight() - columnGap) / 2;
        for (int i = 0; i < shownParams; ++i)
        {
            const int row = i / perRow, place = i % perRow;
            const int x = area.getX() + (area.getWidth() + columnGap) * place / perRow;
            const int nextX = area.getX() + (area.getWidth() + columnGap) * (place + 1) / perRow;
            columns[(size_t)i].setBounds(x, area.getY() + row * (rowHeight + columnGap), nextX - x - columnGap, rowHeight);
        }
    }

    paintBackground();
}

void NuminosEditor::paintBackground()
{
    using C = NuminosLook::Colours;
    if (getWidth() <= 0 || getHeight() <= 0)
        return;
    background = juce::Image(juce::Image::RGB, getWidth(), getHeight(), true);
    juce::Graphics g(background);
    const auto b = getLocalBounds().toFloat();
    const float w = b.getWidth(), h = b.getHeight();

    // obsidian: faintly lit from the top left, falling to black at the far corner
    juce::ColourGradient stone(C::obsidianLit, w * 0.3f, 0.0f, C::obsidianEdge, w * 0.3f + w * 1.1f, h, true);
    stone.addColour(0.45, C::obsidian);
    g.setGradientFill(stone);
    g.fillAll();

    // the sheen across the glassy surface, a faint pool of honey light in the top-left corner,
    // a little warmth rising from below, and a vignette pulling the edges into the dark
    juce::ColourGradient sheen(juce::Colours::white.withAlpha(0.035f), 0.0f, 0.0f,
                               juce::Colours::white.withAlpha(0.015f), w, h * 0.75f, false);
    sheen.addColour(0.26, juce::Colours::transparentWhite);
    sheen.addColour(0.72, juce::Colours::transparentWhite);
    g.setGradientFill(sheen);
    g.fillAll();
    // A warm, diffuse light falling from the top left across the upper half (kWarmth sets how much;
    // 0 is pure black stone), with a brighter pool in the corner it comes from.
    g.setGradientFill(glowAt({w * 0.3f, h * 0.18f}, w * 0.85f, juce::Colour(0xffe2a356).withAlpha(0.05f * kWarmth)));
    g.fillAll();
    g.setGradientFill(glowAt({90.0f, 40.0f}, 280.0f, juce::Colour(0xffe2a356).withAlpha(0.06f * kWarmth)));
    g.fillAll();
    g.setGradientFill(glowAt({w * 0.5f, h + 40.0f}, 360.0f, juce::Colour(0xffff9632).withAlpha(0.07f)));
    g.fillAll();
    juce::ColourGradient vignette(juce::Colours::transparentBlack, b.getCentre(), juce::Colours::black.withAlpha(0.7f), {0.0f, 0.0f}, true);
    vignette.addColour(0.4, juce::Colours::transparentBlack);
    g.setGradientFill(vignette);
    g.fillAll();

    // the wordmark: the honey drop, then NUMINOS in letter-spaced small caps
    auto header = getLocalBounds().reduced(marginX, marginY).removeFromTop(headerHeight).toFloat();
    auto dropArea = header.removeFromLeft(11.0f).withSizeKeepingCentre(11.0f, 15.0f);
    g.setGradientFill(glowAt(dropArea.getCentre().translated(0.0f, 2.0f), 11.0f, C::honey.withAlpha(0.3f))); // its soft glow
    g.fillRect(dropArea.expanded(12.0f));
    g.setColour(C::honey);
    g.fillPath(NuminosLook::dropShape(dropArea));
    header.removeFromLeft(9.0f);
    g.setColour(C::wordmark);
    g.setFont(look.caps(14.0f, 0.22f));
    g.drawText("Numinos", header, juce::Justification::centredLeft);
}

void NuminosEditor::paint(juce::Graphics& g)
{
    g.drawImageAt(background, 0, 0);

    // The meter's light spilling onto the stone around it: further while the Numin is working.
    if (!glowArea.isEmpty())
    {
        const auto area = glowArea.toFloat();
        const float rx = area.getWidth() * 0.5f;
        juce::Graphics::ScopedSaveState state(g);
        g.addTransform(juce::AffineTransform::scale(1.0f, area.getHeight() / area.getWidth(), area.getCentreX(), area.getCentreY()));
        g.setGradientFill(glowAt(area.getCentre(), rx, juce::Colour(0xffffa03c).withAlpha((0.5f + 0.5f * glow) * 0.10f * look.light)));
        g.fillEllipse(area.withSizeKeepingCentre(area.getWidth(), area.getWidth()));
    }

    if (showingAbout)
        NuminosLook::drawWell(g, aboutText.getBounds().expanded(18, 14).toFloat(), 18.0f);
}

void NuminosEditor::timerCallback()
{
    // The light drains from the resin while bypassed (you're hearing the dry sound), and comes back.
    const float target = bypassButton.getToggleState() ? kBypassedLight : 1.0f;
    if (std::abs(look.light - target) > 0.005f)
    {
        look.light += (target - look.light) * 0.2f;
        repaint();
    }

    // The meter's glow on the stone follows how hard the Numin is working.
    const float newGlow = glow + (meter.activity() - glow) * 0.15f;
    if (std::abs(newGlow - glow) > 0.01f)
    {
        glow = newGlow;
        repaint(glowArea);
    }

    if (++ticks % 8 != 0) // the rest runs about four times a second
        return;

    // Pick up changes made outside the UI (session restore, host recall).
    if (processor.describeNumin().name != shownNumin)
        rebuildForNumin();

    if (processor.isStandalone())
    {
        playButton.setToggleState(processor.player.isPlaying(), juce::dontSendNotification);
        if (processor.player.getFile().getFileName() != sampleLabel.getText())
            refreshSampleLabel();
        processor.saveLabSessionIfChanged();
    }
}

void NuminosEditor::chooseSample()
{
    auto samples = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    chooser = std::make_unique<juce::FileChooser>("Choose a sample", samples, "*.wav;*.aif;*.aiff;*.flac;*.mp3");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc) {
                             if (fc.getResult().existsAsFile())
                                 refreshSampleLabel(processor.loadSample(fc.getResult()));
                         });
}

void NuminosEditor::filesDropped(const juce::StringArray& files, int, int)
{
    refreshSampleLabel(processor.loadSample(juce::File(files[0])));
}

void NuminosEditor::refreshSampleLabel(const juce::String& error)
{
    auto file = processor.player.getFile();
    sampleLabel.setText(error.isNotEmpty()            ? error
                        : file.existsAsFile()         ? file.getFileName()
                                                      : "Drop an audio file here, or Load sample...",
                        juce::dontSendNotification);
}

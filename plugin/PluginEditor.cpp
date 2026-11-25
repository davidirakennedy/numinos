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
} // namespace

NuminosEditor::NuminosEditor(NuminosProcessor& p)
    : AudioProcessorEditor(p), processor(p),
      meter(look, [this](int i) { return processor.meterValue(i); }),
      needle(look, [this](int i) { return processor.meterValue(i); })
{
    // Paper grain: faint random specks of ink, tiled behind everything (see paint()).
    grain = juce::Image(juce::Image::ARGB, 128, 128, true);
    juce::Random random(7); // fixed seed: the same paper every time
    for (int y = 0; y < grain.getHeight(); ++y)
        for (int x = 0; x < grain.getWidth(); ++x)
            grain.setPixelAt(x, y, NuminosLook::Colours::ink.withAlpha(random.nextFloat() * 0.06f));

    // ---- Numin menu, grouped by category -----------------------------------
    std::map<juce::String, juce::Array<int>> byCategory;
    const auto& all = numinos::allNumins();
    for (int i = 0; i < (int)all.size(); ++i)
        byCategory[all[(size_t)i].category].add(i);
    for (auto& [category, indices] : byCategory)
    {
        numinMenu.addSectionHeading(category);
        for (int i : indices)
            numinMenu.addItem(all[(size_t)i].name, i + 1);
    }
    numinMenu.onChange = [this] {
        auto name = numinMenu.getText();
        if (name.isNotEmpty() && name != shownNumin)
        {
            processor.setNumin(name, true);
            rebuildForNumin();
        }
    };
    addAndMakeVisible(numinMenu);

    blurb.setColour(juce::Label::textColourId, NuminosLook::Colours::indigo);
    blurb.setFont(look.italic(19.0f));
    blurb.setBorderSize({});
    addAndMakeVisible(blurb);

    // ---- one row per generic parameter ---------------------------------------
    for (int i = 0; i < numinos::kMaxParams; ++i)
    {
        auto& row = rows[(size_t)i];
        row.slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, valueWidth, rowHeight - 6);
        row.label.setFont(look.caps(16.0f, 0.06f));
        row.label.setColour(juce::Label::textColourId, NuminosLook::Colours::graphite);
        row.label.setBorderSize({});
        row.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.params, "p" + juce::String(i + 1), row.slider);
        row.slider.setDoubleClickReturnValue(true, 0.5); // updated per Numin below
        addChildComponent(row.label);
        addChildComponent(row.slider);
    }

    bypassButton.setClickingTogglesState(true);
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
    aboutText.setFont(look.serif(18.0f));
    aboutText.setScrollBarThickness(6);
    addChildComponent(aboutText);
    addChildComponent(meter);
    addChildComponent(needle);

    // ---- standalone lab: loop a sample through the Numin ---------------------
    if (processor.isStandalone())
    {
        loadButton.onClick = [this] { chooseSample(); };
        playButton.setClickingTogglesState(true);
        playButton.onClick = [this] { processor.player.setPlaying(playButton.getToggleState()); };
        sampleLabel.setColour(juce::Label::textColourId, NuminosLook::Colours::graphite);
        sampleLabel.setFont(look.italic(17.0f));
        addAndMakeVisible(loadButton);
        addAndMakeVisible(playButton);
        addAndMakeVisible(sampleLabel);
        refreshSampleLabel();
    }

    setLookAndFeel(&look); // set last, so every child above is restyled (sliders rebuild their value boxes)
    rebuildForNumin();
    startTimerHz(4);
}

NuminosEditor::~NuminosEditor()
{
    setLookAndFeel(nullptr);
    if (processor.isStandalone())
        processor.saveLabSessionIfChanged();
}

void NuminosEditor::rebuildForNumin()
{
    const auto view = processor.describeNumin();
    shownNumin = view.name;

    for (int i = 0; i < (int)numinos::allNumins().size(); ++i)
        if (view.name == numinos::allNumins()[(size_t)i].name)
            numinMenu.setSelectedId(i + 1, juce::dontSendNotification);
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
    needle.configure(view);
    needle.setVisible(needle.hasGain() && !showingAbout);

    auto numin = numinos::createNumin(view.name.toStdString()); // for default values
    for (int i = 0; i < numinos::kMaxParams; ++i)
    {
        auto& row = rows[(size_t)i];
        const bool used = i < view.paramNames.size();
        row.label.setVisible(used && !showingAbout);
        row.slider.setVisible(used && !showingAbout);
        if (used)
        {
            row.label.setText(view.paramNames[i], juce::dontSendNotification);
            row.slider.setDoubleClickReturnValue(true, numin->paramInfo(i).defaultValue);
            row.slider.getProperties().set("mark", numin->paramInfo(i).mark); // drawn by NuminosLook, if >= 0
            row.slider.updateText(); // display text depends on the Numin
        }
    }

    const int lab = processor.isStandalone() ? rowHeight + gap : 0;
    const int metering = meter.isVisible() || needle.isVisible() ? meterHeight + gap : 0;
    const int body = showingAbout ? aboutHeight : metering + rowHeight * view.paramNames.size();
    setSize(width, margin + headerHeight + gap + titleHeight + blurbHeight + gap * 2 + lab + body + margin);
    resized();
}

void NuminosEditor::showAbout(bool show)
{
    showingAbout = show;
    rebuildForNumin(); // swaps the sliders for the text (or back) and resizes the window
}

void NuminosEditor::resized()
{
    auto area = getLocalBounds().reduced(margin);

    // header: the wordmark is painted on the left (paint()); buttons sit on the right
    auto header = area.removeFromTop(headerHeight);
    bypassButton.setBounds(header.removeFromRight(118));
    if (aboutButton.isVisible())
    {
        header.removeFromRight(8);
        aboutButton.setBounds(header.removeFromRight(76));
    }
    area.removeFromTop(gap);

    numinMenu.setBounds(area.removeFromTop(titleHeight));
    blurb.setBounds(area.removeFromTop(blurbHeight));
    area.removeFromTop(gap);
    ruleY = area.getY();
    area.removeFromTop(gap);

    if (processor.isStandalone())
    {
        auto labRow = area.removeFromTop(rowHeight).reduced(0, 3);
        loadButton.setBounds(labRow.removeFromLeft(140));
        labRow.removeFromLeft(8);
        playButton.setBounds(labRow.removeFromLeft(70));
        labRow.removeFromLeft(12);
        sampleLabel.setBounds(labRow);
        area.removeFromTop(gap);
    }

    if (showingAbout)
    {
        aboutText.setBounds(area);
        return;
    }

    if (meter.isVisible() || needle.isVisible())
    {
        auto meters = area.removeFromTop(meterHeight);
        if (needle.isVisible())
        {
            needle.setBounds(meters.removeFromRight(meter.isVisible() ? needleWidth : meters.getWidth()));
            meters.removeFromRight(gap);
        }
        meter.setBounds(meters);
        area.removeFromTop(gap);
    }

    for (auto& row : rows)
    {
        if (!row.slider.isVisible())
            continue;
        auto r = area.removeFromTop(rowHeight);
        row.label.setBounds(r.removeFromLeft(labelWidth));
        row.slider.setBounds(r);
    }
}

void NuminosEditor::paint(juce::Graphics& g)
{
    using C = NuminosLook::Colours;
    const auto bounds = getLocalBounds().toFloat();

    // paper, grain, and the warm darkening toward the edges (the site's body::after)
    g.fillAll(C::paper);
    g.setTiledImageFill(grain, 0, 0, 1.0f);
    g.fillAll();
    juce::ColourGradient edge(juce::Colours::transparentBlack, bounds.getCentre(),
                              juce::Colour(0xff464a52).withAlpha(0.12f), bounds.getTopLeft(), true);
    edge.addColour(0.6, juce::Colours::transparentBlack);
    g.setGradientFill(edge);
    g.fillAll();

    // wordmark: the honey drop, then NUMINOS in letter-spaced small caps (the site's imprint)
    auto header = getLocalBounds().reduced(margin).removeFromTop(headerHeight).toFloat();
    auto dropArea = header.removeFromLeft(10.0f).withSizeKeepingCentre(10.0f, 15.0f);
    g.setColour(C::amber);
    g.fillPath(NuminosLook::dropShape(dropArea));
    header.removeFromLeft(9.0f);
    g.setColour(C::graphite);
    g.setFont(look.caps(14.0f, 0.18f));
    g.drawText("Numinos", header, juce::Justification::centredLeft);

    // the rule under the Numin's name
    g.setColour(C::hairline);
    g.fillRect(juce::Rectangle<float>((float)margin, (float)ruleY, bounds.getWidth() - 2.0f * margin, 1.0f));
}

void NuminosEditor::timerCallback()
{
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

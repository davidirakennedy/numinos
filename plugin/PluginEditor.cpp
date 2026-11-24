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

NuminosEditor::NuminosEditor(NuminosProcessor& p) : AudioProcessorEditor(p), processor(p)
{
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

    blurb.setColour(juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible(blurb);

    // ---- one row per generic parameter ---------------------------------------
    for (int i = 0; i < numinos::kMaxParams; ++i)
    {
        auto& row = rows[(size_t)i];
        row.slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 130, rowHeight - 6);
        row.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.params, "p" + juce::String(i + 1), row.slider);
        row.slider.setDoubleClickReturnValue(true, 0.5); // updated per Numin below
        addChildComponent(row.label);
        addChildComponent(row.slider);
    }

    bypassButton.setClickingTogglesState(true);
    bypassButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::darkred);
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
    aboutText.setFont(juce::Font(juce::FontOptions(15.0f)));
    addChildComponent(aboutText);

    // ---- standalone lab: loop a sample through the Numin ---------------------
    if (processor.isStandalone())
    {
        loadButton.onClick = [this] { chooseSample(); };
        playButton.setClickingTogglesState(true);
        playButton.onClick = [this] { processor.player.setPlaying(playButton.getToggleState()); };
        sampleLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
        addAndMakeVisible(loadButton);
        addAndMakeVisible(playButton);
        addAndMakeVisible(sampleLabel);
        refreshSampleLabel();
    }

    rebuildForNumin();
    startTimerHz(4);
}

NuminosEditor::~NuminosEditor()
{
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
            row.slider.updateText(); // display text depends on the Numin
        }
    }

    const int sampleRows = processor.isStandalone() ? 1 : 0;
    const int body = showingAbout ? aboutHeight : rowHeight * view.paramNames.size();
    setSize(560, margin * 2 + rowHeight * (2 + sampleRows) + body + margin * (1 + sampleRows));
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

    auto top = area.removeFromTop(rowHeight);
    bypassButton.setBounds(top.removeFromRight(120));
    top.removeFromRight(margin);
    if (aboutButton.isVisible())
    {
        aboutButton.setBounds(top.removeFromRight(70));
        top.removeFromRight(margin);
    }
    numinMenu.setBounds(top);
    blurb.setBounds(area.removeFromTop(rowHeight));

    if (processor.isStandalone())
    {
        area.removeFromTop(margin / 2);
        auto lab = area.removeFromTop(rowHeight);
        loadButton.setBounds(lab.removeFromLeft(130));
        lab.removeFromLeft(6);
        playButton.setBounds(lab.removeFromLeft(70));
        lab.removeFromLeft(6);
        sampleLabel.setBounds(lab);
    }
    area.removeFromTop(margin);

    if (showingAbout)
    {
        aboutText.setBounds(area);
        return;
    }

    for (auto& row : rows)
    {
        if (!row.slider.isVisible())
            continue;
        auto r = area.removeFromTop(rowHeight);
        row.label.setBounds(r.removeFromLeft(110));
        row.slider.setBounds(r);
    }
}

void NuminosEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
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

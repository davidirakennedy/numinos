#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
juce::String idFor(int i) { return "p" + juce::String(i + 1); }

// A generic parameter whose *name* follows the current Numin, so the DAW's
// automation lanes say "Viscosity" instead of "Param 1".
class NuminParameter : public juce::AudioParameterFloat
{
  public:
    NuminParameter(NuminosProcessor& p, int i)
        : juce::AudioParameterFloat(
              juce::ParameterID{idFor(i), 1}, "Param " + juce::String(i + 1),
              juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f,
              juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                  [&p, i](float v, int) { return p.paramText(i, v); })),
          processor(p), index(i)
    {
    }

    juce::String getName(int maxLength) const override
    {
        return processor.paramName(index).substring(0, maxLength);
    }

  private:
    NuminosProcessor& processor;
    int index;
};
} // namespace

NuminosProcessor::NuminosProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "Numinos", makeLayout())
{
    for (int i = 0; i < numinos::kMaxParams; ++i)
        paramValues[i] = params.getRawParameterValue(idFor(i));
    bypassValue = params.getRawParameterValue("bypass");

    setNumin(numinos::allNumins().front().name, true);

    if (isStandalone())
    {
        // Restore the last lab session (Numin, knobs, sample). Posted async so it
        // runs after JUCE's standalone wrapper restores its own (older) state.
        juce::MessageManager::callAsync([safe = juce::WeakReference<NuminosProcessor>(this), this] {
            if (safe == nullptr)
                return;
            if (auto xml = juce::XmlDocument::parse(labSessionFile()))
                restoreFromXml(*xml);
        });
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout NuminosProcessor::makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int i = 0; i < numinos::kMaxParams; ++i)
        layout.add(std::make_unique<NuminParameter>(*this, i));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass", 1}, "Bypass", false));
    return layout;
}

//==============================================================================
void NuminosProcessor::setNumin(const juce::String& name, bool resetParams)
{
    auto fresh = numinos::createNumin(name.toStdString());
    if (fresh == nullptr)
        return;

    fresh->prepare(sampleRate);
    const int freshLatency = fresh->latencySamples();
    {
        const juce::SpinLock::ScopedLockType lock(numinLock);
        std::swap(numin, fresh);
    } // the old Numin (now in `fresh`) is destroyed here, outside the lock

    latency.store(freshLatency);
    setLatencySamples(freshLatency); // tell the host, so it can compensate

    if (resetParams)
    {
        const auto view = describeNumin();
        for (int i = 0; i < numinos::kMaxParams; ++i)
        {
            float v = i < view.paramNames.size() ? numin->paramInfo(i).defaultValue : 0.0f;
            params.getParameter(idFor(i))->setValueNotifyingHost(v);
        }
    }
    updateHostDisplay(ChangeDetails().withParameterInfoChanged(true));
}

NuminosProcessor::NuminView NuminosProcessor::describeNumin() const
{
    const juce::SpinLock::ScopedLockType lock(numinLock);
    NuminView v;
    if (numin == nullptr)
        return v;
    v.name = numin->name();
    v.category = numin->category();
    v.blurb = numin->blurb();
    for (int i = 0; i < numin->numParams(); ++i)
        v.paramNames.add(numin->paramInfo(i).name);
    return v;
}

juce::String NuminosProcessor::paramText(int index, float value) const
{
    const juce::SpinLock::ScopedLockType lock(numinLock);
    if (numin == nullptr || index >= numin->numParams())
        return "-";
    return numin->displayValue(index, value);
}

juce::String NuminosProcessor::paramName(int index) const
{
    const juce::SpinLock::ScopedLockType lock(numinLock);
    if (numin == nullptr || index >= numin->numParams())
        return "(unused " + juce::String(index + 1) + ")";
    return numin->paramInfo(index).name;
}

//==============================================================================
void NuminosProcessor::prepareToPlay(double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;
    scratch.assign((size_t)juce::jmax(samplesPerBlock, 4096), 0.0f);
    player.prepare(newSampleRate);
    dryDelay.setSize(2, (int)(kMaxLatencySeconds * newSampleRate) + 1);
    dryDelay.clear();
    dryWritePos = 0;

    int newLatency = 0;
    {
        const juce::SpinLock::ScopedLockType lock(numinLock);
        if (numin != nullptr)
        {
            numin->prepare(newSampleRate);
            newLatency = numin->latencySamples(); // depends on the sample rate
        }
    }
    latency.store(newLatency);
    setLatencySamples(newLatency);
}

bool NuminosProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Stereo -> stereo, plus mono -> mono and mono -> stereo for mono tracks.
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
}

void NuminosProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    const int frames = buffer.getNumSamples();

    if (numIn == 1 && numOut == 2) // mono track: duplicate into the right channel
        buffer.copyFrom(1, 0, buffer, 0, 0, frames);

    if (isStandalone())
        player.render(buffer); // replaces the input while a sample is playing

    writeDry(buffer, frames); // always, so bypass has history to play from

    const juce::SpinLock::ScopedTryLockType lock(numinLock);
    if (!lock.isLocked() || numin == nullptr) // the UI is mid-swap; pass dry audio through
    {
        readDelayedDry(buffer, frames);
        return;
    }

    // The Numin runs even when bypassed: a Numin with lookahead must keep hearing the
    // audio, or un-bypassing would play seconds of stale sound from before.
    for (int i = 0; i < numin->numParams(); ++i)
        numin->setParam(i, paramValues[i]->load());

    float* left = buffer.getWritePointer(0);
    if (numOut >= 2)
    {
        float* right = buffer.getWritePointer(1);
        numin->process(left, right, left, right, frames);
    }
    else
    {
        // Mono: feed a copy of the signal to the right side, keep the left output.
        for (int pos = 0; pos < frames; pos += (int)scratch.size())
        {
            const int n = juce::jmin((int)scratch.size(), frames - pos);
            std::copy(left + pos, left + pos + n, scratch.begin());
            numin->process(left + pos, scratch.data(), left + pos, scratch.data(), n);
        }
    }

    if (*bypassValue > 0.5f)
        readDelayedDry(buffer, frames); // dry, but exactly as late as the processed sound
}

void NuminosProcessor::writeDry(const juce::AudioBuffer<float>& buffer, int frames)
{
    const int size = dryDelay.getNumSamples();
    if (size == 0)
        return;
    const int channels = juce::jmin(buffer.getNumChannels(), dryDelay.getNumChannels());
    for (int ch = 0; ch < channels; ++ch)
    {
        const float* in = buffer.getReadPointer(ch);
        float* d = dryDelay.getWritePointer(ch);
        for (int n = 0, w = dryWritePos; n < frames; ++n, w = (w + 1) % size)
            d[w] = in[n];
    }
    dryWritePos = (dryWritePos + frames) % size;
}

void NuminosProcessor::readDelayedDry(juce::AudioBuffer<float>& buffer, int frames)
{
    const int size = dryDelay.getNumSamples();
    if (size == 0)
        return;
    const int delay = juce::jlimit(0, size - 1, latency.load());
    const int channels = juce::jmin(buffer.getNumChannels(), dryDelay.getNumChannels());
    const int start = dryWritePos - frames; // where this block's input was written
    for (int ch = 0; ch < channels; ++ch)
    {
        float* out = buffer.getWritePointer(ch);
        const float* d = dryDelay.getReadPointer(ch);
        for (int n = 0; n < frames; ++n)
            out[n] = d[(((start + n - delay) % size) + size) % size];
    }
}

juce::AudioProcessorParameter* NuminosProcessor::getBypassParameter() const
{
    return params.getParameter("bypass");
}

//==============================================================================
std::unique_ptr<juce::XmlElement> NuminosProcessor::stateXml()
{
    auto state = params.copyState();
    state.setProperty("numin", describeNumin().name, nullptr);
    if (isStandalone())
        state.setProperty("sample", player.getFile().getFullPathName(), nullptr);
    return state.createXml();
}

void NuminosProcessor::restoreFromXml(const juce::XmlElement& xml)
{
    if (!xml.hasTagName(params.state.getType()))
        return;
    auto state = juce::ValueTree::fromXml(xml);
    setNumin(state.getProperty("numin").toString(), false);
    params.replaceState(state);

    const juce::File sample(state.getProperty("sample").toString());
    if (isStandalone() && sample.existsAsFile())
        loadSample(sample);
}

void NuminosProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    copyXmlToBinary(*stateXml(), destData);
}

void NuminosProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        restoreFromXml(*xml);
}

//==============================================================================
juce::String NuminosProcessor::loadSample(const juce::File& f)
{
    auto error = player.load(f);
    if (error.isEmpty())
        player.setPlaying(true);
    return error;
}

juce::File NuminosProcessor::labSessionFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Numinos/lab-session.xml");
}

void NuminosProcessor::saveLabSessionIfChanged()
{
    auto text = stateXml()->toString();
    if (text == lastSavedLabSession)
        return;
    labSessionFile().getParentDirectory().createDirectory();
    if (labSessionFile().replaceWithText(text))
        lastSavedLabSession = text;
}

juce::AudioProcessorEditor* NuminosProcessor::createEditor() { return new NuminosEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NuminosProcessor(); }

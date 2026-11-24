// SamplePlayer — loops an audio file into the plugin's input.
// Only used when Numinos runs as a standalone app (the "lab"), so you can
// audition Numins on your own recordings without opening a DAW.
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

class SamplePlayer
{
  public:
    SamplePlayer() { formats.registerBasicFormats(); } // wav, aiff, flac, ogg, mp3 (macOS)

    // Message thread. Returns an error message, or empty on success.
    juce::String load(const juce::File& file)
    {
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (reader == nullptr)
            return "Can't read " + file.getFileName();

        source.setSize(2, (int)reader->lengthInSamples);
        reader->read(&source, 0, (int)reader->lengthInSamples, 0, true, true); // mono fills both
        sourceRate = reader->sampleRate;
        loadedFile = file;
        rebuild();
        return {};
    }

    // Called from prepareToPlay: resample for the device's rate.
    void prepare(double newDeviceRate)
    {
        if (!juce::exactlyEqual(newDeviceRate, deviceRate))
        {
            deviceRate = newDeviceRate;
            rebuild();
        }
    }

    // Audio thread: overwrite the first two channels of `buffer` with the loop.
    void render(juce::AudioBuffer<float>& buffer)
    {
        const juce::SpinLock::ScopedTryLockType lock(swapLock);
        if (!lock.isLocked() || !playing || playback.getNumSamples() == 0)
            return;

        const int len = playback.getNumSamples();
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            for (int ch = 0; ch < juce::jmin(2, buffer.getNumChannels()); ++ch)
                buffer.setSample(ch, i, playback.getSample(ch, position));
            position = (position + 1) % len;
        }
    }

    void setPlaying(bool shouldPlay)
    {
        if (shouldPlay && !playing)
            position = 0; // restart from the top, like a transport
        playing = shouldPlay;
    }
    bool isPlaying() const { return playing; }
    juce::File getFile() const { return loadedFile; }

  private:
    void rebuild()
    {
        if (source.getNumSamples() == 0 || sourceRate <= 0)
            return;

        const double ratio = sourceRate / deviceRate;
        juce::AudioBuffer<float> resampled(2, (int)(source.getNumSamples() / ratio));
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::LagrangeInterpolator interp;
            interp.process(ratio, source.getReadPointer(ch), resampled.getWritePointer(ch),
                           resampled.getNumSamples());
        }

        const juce::SpinLock::ScopedLockType lock(swapLock);
        std::swap(playback, resampled); // old buffer is freed here, off the audio thread
        position = 0;
    }

    juce::AudioFormatManager formats;
    juce::AudioBuffer<float> source, playback;
    double sourceRate = 0, deviceRate = 44100;
    juce::File loadedFile;
    juce::SpinLock swapLock;
    std::atomic<bool> playing{false};
    int position = 0;
};

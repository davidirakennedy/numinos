// Ear — Honey's hearing: how loud each 5 ms frame *sounds*, and how vowel-like it is.
//
// A loudness meter's K-weighting is one filter and one power sum. It's built for whole mixes, and on
// a voice it hears the wrong things: sibilance and consonants read loud (K-weighting lifts
// everything above ~2 kHz by 4 dB, right where "s" and "t" live), while what we actually judge a
// syllable by is its vowel. The Ear is a small step closer to how hearing works:
//
//   1. Bands. The voice is split into five bands, roughly 1.3 octaves each, from the chest of the
//      voice (~200 Hz) to its air (~7 kHz).
//   2. Sensitivity. Each band is weighted by how sensitive the ear is there at a conversational
//      level (a rough reading of the ISO 226 equal-loudness contours): most sensitive around
//      3 kHz, less so in the low mids, and noticeably less up where sibilance sits.
//   3. Summation. Each band's power becomes *loudness* through a compressive power law (Stevens'
//      law: loudness ~ power^0.3), and the bands' loudnesses add. So sound spread across the
//      spectrum (a vowel, rich in harmonics) is louder than the same power squeezed into one band
//      (a hiss). This is the core of how loudness models (Zwicker, Moore-Glasberg) work, kept tiny.
//   4. Vowel-ness. The share of a frame's power below ~2 kHz says how voiced it is: near 1 in a
//      vowel, near 0 in an "s". The Segmenter uses it to judge each segment by its vowel.
//
// The output is "intensity": a linear quantity whose *average* over time is meaningful (loudness
// adds up over time roughly like this), and toDb() turns it into dB. For a steady sound in one band
// that's an ordinary dB scale, so everything downstream (noise floor, margins) keeps working in dB.
#pragma once

#include "Segment.h"

#include "../dsp/Biquad.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace numinos::honey
{

class Ear
{
  public:
    void prepare(double sampleRate)
    {
        frameLength = std::max(1, (int)std::lround(kFrameSeconds * sampleRate));
        for (int b = 0; b < kBands; ++b)
        {
            left[b] = dsp::Biquad::bandPass(kBandHz[b], kBandQ, sampleRate);
            right[b] = left[b];
        }
        reset();
    }

    void reset()
    {
        for (int b = 0; b < kBands; ++b)
        {
            left[b].reset();
            right[b].reset();
        }
        bandPower.fill(0.0);
        sampleCount = 0;
        lastIntensity = 0.0;
        lastVowel = 0.0;
    }

    int samplesPerFrame() const { return frameLength; }

    // Feed one stereo sample. Returns true when a frame is complete; then read intensity() and vowel().
    bool process(double l, double r)
    {
        for (int b = 0; b < kBands; ++b)
        {
            const double yl = left[b].process(l), yr = right[b].process(r);
            bandPower[b] += 0.5 * (yl * yl + yr * yr);
        }
        if (++sampleCount < frameLength)
            return false;

        double loudness = 0.0, total = 0.0, low = 0.0;
        for (int b = 0; b < kBands; ++b)
        {
            const double power = bandPower[b] / frameLength;
            loudness += std::pow(kBandWeight[b] * power, kExponent);
            total += power;
            if (b < kVowelBands)
                low += power;
        }
        lastIntensity = loudness;
        const double share = total > 1.0e-15 ? low / total : 0.0;
        lastVowel = std::clamp((share - kVowelFrom) / (kVowelTo - kVowelFrom), 0.0, 1.0);

        bandPower.fill(0.0);
        sampleCount = 0;
        return true;
    }

    // The frame's loudness, as intensity (average it over time, then toDb()).
    double intensity() const { return lastIntensity; }

    // How vowel-like the frame is: 1 in a vowel, 0 in a hiss or a click.
    double vowel() const { return lastVowel; }

    // Intensity to dB. Inverts the power law, so a steady sound in one band reads as its weighted
    // power in dB; kCalibrationDb sets the whole scale so a sung vowel reads about where K-weighting
    // read it, which keeps the Noise floor range and the meter familiar.
    static double toDb(double intensity) { return (10.0 / kExponent) * std::log10(intensity + 1.0e-30) + kCalibrationDb; }

  private:
    static constexpr int kBands = 5;
    static constexpr double kBandHz[kBands] = {200.0, 500.0, 1250.0, 3000.0, 7000.0};
    static constexpr double kBandQ = 1.0; // about 1.4 octaves wide: neighbouring bands just overlap

    // How sensitive the ear is in each band, relative to 1.25 kHz (dB, as power weights below).
    // Read roughly off the 60-phon equal-loudness contour.
    static constexpr double kBandWeightDb[kBands] = {-7.0, -2.0, 0.0, 3.0, -3.0};
    static inline const std::array<double, kBands> kBandWeight = [] {
        std::array<double, kBands> w{};
        for (int b = 0; b < kBands; ++b)
            w[b] = std::pow(10.0, kBandWeightDb[b] / 10.0);
        return w;
    }();

    // Stevens' power law on power. Smaller = more summation across bands (Zwicker uses ~0.23).
    static constexpr double kExponent = 0.3;
    static constexpr double kCalibrationDb = -13.0; // measured: puts sung vowels where K-weighting read them

    // A frame with at least kVowelTo of its power in the lowest three bands (below ~2 kHz) counts
    // as fully vowel; one with kVowelFrom or less, not at all.
    static constexpr int kVowelBands = 3;
    static constexpr double kVowelFrom = 0.35, kVowelTo = 0.75;

    int frameLength = 1;
    std::array<dsp::Biquad, kBands> left, right;
    std::array<double, kBands> bandPower{};
    int sampleCount = 0;
    double lastIntensity = 0.0, lastVowel = 0.0;
};

} // namespace numinos::honey

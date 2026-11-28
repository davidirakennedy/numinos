// Response — how Kite hears a sound (for now only for the meter), and the "film" Bloom develops on
// (kept from Bloom, unused here).
//
//   Follower — hears the voice frame by frame and says how loud it sounds around any moment.
//   Film     — turns that loudness (plus Exposure) into how much to bloom, 0 .. 1.
#pragma once

#include "../dsp/Ear.h"
#include "../dsp/FrameHistory.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace numinos::kite
{

// Kite hears in 5 ms frames, like Honey and Bloom.
constexpr double kFrameSeconds = 0.005;

// ---- Follower -------------------------------------------------------------------------------
//
// Around every frame it takes the average loudness over a short window that looks exactly as far
// ahead as behind (a triangle, centred on the frame). Rises and falls are treated alike, so there
// is no attack and no release: the bloom can't arrive after the voice does, or hang on after it
// has gone. That's what lets Bloom (where this came from) move within a line, syllable by syllable, without ever being
// heard as moving: it moves *with* the voice, the way a halo is brighter exactly when the light
// is. The cost is a little latency: the audio is delayed to line up with the window's centre.
class Follower
{
  public:
    void prepare(double sampleRate)
    {
        frameLength = std::max(1, (int)std::lround(kFrameSeconds * sampleRate));
        halfWidth = std::max(1, (int)std::lround(kHalfWidthSeconds / kFrameSeconds));
        const int capacity = 4 * halfWidth + 8;
        intensity.prepare(capacity);
        heardDb.prepare(capacity);
        reset();
    }

    void reset()
    {
        intensity.reset();
        heardDb.reset();
        newestFrame = -1;
    }

    int samplesPerFrame() const { return frameLength; }

    // How late the audio must play for every moment's window to be complete when it's heard: the
    // window's forward half, plus a frame for interpolating, plus the frame still being heard.
    int latencySamples() const { return (halfWidth + 2) * frameLength; }

    // Feed every frame the Ear finishes (its intensity).
    void addFrame(double frameIntensity)
    {
        ++newestFrame;
        intensity[newestFrame] = frameIntensity;

        // The newest frame completes the window of the frame halfWidth before it.
        const int64_t centre = newestFrame - halfWidth;
        double sumWeight = 0.0, sumIntensity = 0.0;
        for (int j = -halfWidth; j <= halfWidth; ++j)
        {
            const int64_t f = centre + j;
            if (f < 0)
                continue; // before the start: silence
            const double w = halfWidth + 1 - std::abs(j); // the triangle
            sumWeight += w;
            sumIntensity += w * intensity[f];
        }
        heardDb[centre] = dsp::Ear::toDb(sumWeight > 0.0 ? sumIntensity / sumWeight : 0.0);
    }

    // How loud the voice sounds around a given sample (dB, as the Ear hears it; counted from the
    // start), interpolated between the frames on either side. Only valid for samples at least
    // latencySamples() old.
    double loudnessAt(int64_t sample) const
    {
        // Frame f's centre is at sample f * frameLength + (frameLength - 1) / 2.
        const double x = (sample - 0.5 * (frameLength - 1)) / frameLength;
        const int64_t f = (int64_t)std::floor(x);
        const double t = x - (double)f;
        if (f < 0 || f + 1 > newestFrame - halfWidth)
            return kSilenceDb;
        return heardDb[f] + (heardDb[f + 1] - heardDb[f]) * t;
    }

  private:
    // Half the window: how far ahead (and behind) it listens around each moment. Short enough
    // to follow syllables; long enough that a single 5 ms frame never jerks the bloom.
    static constexpr double kHalfWidthSeconds = 0.025;
    static constexpr double kSilenceDb = -120.0;

    int frameLength = 1, halfWidth = 1;
    dsp::FrameHistory intensity; // per frame, as the Ear heard it
    dsp::FrameHistory heardDb;   // per frame, averaged over the window around it (dB)
    int64_t newestFrame = -1;
};

// ---- Film -----------------------------------------------------------------------------------
//
// Turns loudness into bloom the way film turns light into density: a characteristic curve with a
// toe (a long, gentle on-ramp, so there's no point where it switches on), a straight middle,
// and a shoulder (it saturates smoothly, so cranking Exposure blooms everything without blowing
// anything out). The curve is fixed. Exposure slides the voice along it, like opening the
// aperture: the same film, more light.
//
// Here the curve is a logistic in dB. A different film (a "stock") would be a different pivot and
// latitude, or a different shape altogether: a longer toe, a harder shoulder.
struct Film
{
    double pivotDb;    // half-bloom: where the exposed loudness gives a bloom of 0.5
    double latitudeDb; // how many dB it takes to go from a 10% bloom to a 90% bloom

    // 0 .. 1: how much a moment at this exposed loudness (loudness + Exposure, in dB) blooms.
    double develop(double exposedDb) const
    {
        const double softness = latitudeDb / (2.0 * std::log(9.0)); // 10% .. 90% spans latitudeDb
        return 1.0 / (1.0 + std::exp(-(exposedDb - pivotDb) / softness));
    }
};

} // namespace numinos::kite

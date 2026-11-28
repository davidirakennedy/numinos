// Body — the voice as a bigger source in itself, before it ever reaches two ears.
//
// A mouth is small: everything it sings comes from one point, at one instant, every harmonic
// moving in lockstep. A big source isn't like that. A choir or a piano heard through a single
// microphone still sounds big, because its sound is made of many slightly different contributions
// that arrive at slightly different times and move slightly independently. Size, in mono, is a
// kind of internal looseness. Body gives the voice some, in two ways, the same in both ears:
//
//   Depth — different parts of the spectrum arrive a hair earlier or later than others (up to a few
//     milliseconds), like sound radiating from a soundboard rather than a point. The amount of
//     each frequency is untouched; only when it arrives changes. The arrival times wander smoothly
//     up the spectrum, never jumping between neighbouring frequencies, so nothing smears into a
//     click or a ring.
//
//   Drift — each harmonic's phase wanders on its own, a little ahead of or behind where lockstep
//     would put it. The harmonics stop moving as one rigid shape and start to breathe loosely, the
//     way many radiating points never quite agree. Each wanders within a fixed bound. Slowly, it
//     reads as life; quickly, perhaps as grain, a fine texture too fast to follow (like film
//     grain, which makes an image feel organic rather than noisy). How fast is a knob, for now.
//
// The lowest part of the voice (its fundamental and the harmonic above) stays as it is: the
// still, sharp centre the rest loosens around.
//
// Depth never changes from moment to moment (only when you move the knob), so it can't bend
// pitch, and it applies to everything. Drift needs to know where the harmonics are, so it only
// touches clearly pitched sound.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace numinos::kite
{

class Body
{
  public:
    // numBins: the spectrum's bins (0 .. size/2); binHz: their spacing; frameSeconds: how often
    // shape() is called, so drift wanders at the same speed at every sample rate.
    void prepare(int numBins, double binHz, double frameSeconds)
    {
        // Depth: an arrival time for every frequency, then the phase that gives exactly those
        // arrival times (the running sum of 2 pi * time * bin width, up the spectrum).
        depthTurn.assign(numBins, 0.0);
        double turn = 0.0;
        for (int k = 1; k < numBins; ++k)
        {
            turn -= 2.0 * M_PI * arrival(k * binHz) * binHz;
            depthTurn[(size_t)k] = turn;
        }
        frameLength = frameSeconds;
        reset();
    }

    void reset()
    {
        random = kSeed;
        for (auto& p : driftPhase)
            p = M_PI * uniform(); // every harmonic starts somewhere different
    }

    // Add this frame's turn (the same for both ears) to every bin.
    //   depthMs: the largest arrival-time difference either way (ms)
    //   reach:   how far each harmonic may wander either way (radians)
    //   driftSeconds: how long it takes to wander about a radian (smaller = faster)
    void shape(double* turns, int numBins, double binHz, double pitchHz, double voiced, double depthMs, double reach,
               double driftSeconds)
    {
        wander(driftSeconds);
        const double depth = depthMs / kShapeMs;
        for (int k = 1; k < numBins; ++k)
            turns[k] += depth * depthTurn[(size_t)k];

        if (voiced <= 0.0 || pitchHz <= 0.0)
            return;
        for (int h = 0; h < kMaxHarmonics; ++h)
            offset[(size_t)h] = voiced * reach * std::sin(driftPhase[(size_t)h]) * anchor(h);

        for (int k = 1; k < numBins; ++k)
        {
            // Drift: between two harmonics, slide smoothly from one's offset to the next's, so the
            // phase never jumps between neighbouring frequencies.
            const double h = std::min(k * binHz / pitchHz, (double)(kMaxHarmonics - 2));
            const int below = (int)h;
            const double t = 0.5 - 0.5 * std::cos(M_PI * (h - below));
            turns[k] += offset[(size_t)below] + (offset[(size_t)below + 1] - offset[(size_t)below]) * t;
        }
    }

  private:
    // Depth's shape: how early (-) or late (+) each frequency arrives, at most kShapeMs. Two slow
    // waves up the spectrum, in octaves, so arrival times change gently from one frequency to the
    // next and the pattern doesn't repeat. Nothing below kStillBelowHz, fading in over an octave.
    static double arrival(double hz)
    {
        if (hz <= kStillBelowHz)
            return 0.0;
        const double octaves = std::log2(hz / kStillBelowHz);
        const double fadeIn = std::min(1.0, octaves);
        const double wave = 0.6 * std::sin(2.0 * M_PI * 0.9 * octaves + 1.0) + 0.4 * std::sin(2.0 * M_PI * 2.3 * octaves + 2.0);
        return fadeIn * wave * kShapeMs * 0.001;
    }

    // Drift leaves the lowest harmonics still: none at the 1st and 2nd, full from the 4th.
    static double anchor(int harmonic) { return std::clamp((harmonic - 2) / 2.0, 0.0, 1.0); }

    // Each harmonic's wander takes one random step around its own circle, drifting about a radian
    // every driftSeconds (uniform steps of +/- step give that for any frame length). How far it
    // has wandered is the sine of where it is, so it can never go past the bound. It can't wander
    // faster than once a frame: at the quickest settings every frame is simply somewhere new.
    void wander(double driftSeconds)
    {
        const double step = std::sqrt(3.0 * frameLength / driftSeconds);
        for (auto& p : driftPhase)
            p += step * uniform();
    }

    // A random number in -1 .. 1 (xorshift): deterministic, and reset() starts it over.
    double uniform()
    {
        random ^= random << 13;
        random ^= random >> 7;
        random ^= random << 17;
        return (double)(random >> 11) * (2.0 / 9007199254740992.0) - 1.0;
    }

    static constexpr double kShapeMs = 1.0;        // depthTurn is built at this size, then scaled
    static constexpr double kStillBelowHz = 300.0; // depth leaves everything below this alone
    static constexpr int kMaxHarmonics = 160;
    static constexpr uint64_t kSeed = 0xD1B54A32D192ED03ull;

    std::vector<double> depthTurn;
    std::array<double, kMaxHarmonics> driftPhase{}, offset{};
    double frameLength = 0.01; // seconds between calls to shape()
    uint64_t random = kSeed;
};

} // namespace numinos::kite

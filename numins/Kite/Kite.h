// Kite — transients swoop in like a scratched record; tails bob and bounce.
//
// I was initially working on another vocal-based plugin I was calling Bloom, and wasn't
// having much luck. For fun, I threw it on a drum track to see what would happen. I
// stumbled upon something pretty cool. Basically, Bloom had an engine that kept track of the
// fundamental frequency, and then gently delayed (and fanned out) the arrival of its harmonics. But on
// something like drums where there are a lot of transients with a lot of frequency information,
// this basically turns those transients into "chirps" or "swoops". Kinda like the sound of a DJ 
// scratching a record. The project was a dead end for the voice, but this serendipitous discovery may
// have some cool applications or evolutions in the future, so I saved it here. Also, I'm calling it Kite
// for now because kites "swoop".
//
//   Drift, pushed far and fast, gives each harmonic a new random phase every frame (~94 times a
//     second). Overlapping frames disagree, so they partly cancel and reinforce: sidebands around
//     each harmonic (the tin-mic garble), a bouncing wobble on pitched tails (snares), and hits
//     smeared across the frame, a little before themselves (the swoop in).
//   Depth makes different frequencies arrive at different times, which turns a click into a fast
//     up-and-down sweep: the whip.
//   Spread leans harmonics between the ears, as in Bloom.
//
// For now it's Bloom's code as it was the moment I discovered it's strange effect on drums. 
//
//   Response.h     — how loud the sound is around each moment (for the meter)
//   ../dsp/Pitch.h — what the sound's pitch is, and how sure that is
//   ../dsp/Spectrum.h — the sound in frequency: one in, two ears out
//   Body.h         — depth and drift, the same in both ears
//   Fan.h          — each harmonic leaning a little between the ears
#pragma once

#include "../Numin.h"
#include "../dsp/DelayLine.h"
#include "../dsp/Ear.h"
#include "../dsp/Pitch.h"

#include "Body.h"
#include "Fan.h"
#include "Response.h"
#include "../dsp/Spectrum.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace numinos
{

class Kite : public Numin
{
  public:
    enum Param { Depth, Drift, DriftSpeed, Spread, NumParams };

    const char* name() const override { return "Kite"; }
    const char* category() const override { return "Texture"; }
    const char* blurb() const override { return "Transients swoop in like a scratched record; tails bob and bounce."; }

    int numParams() const override { return NumParams; }
    ParamInfo paramInfo(int i) const override
    {
        static const ParamInfo info[NumParams] = {
            {"Depth", 1.0f / 3.0f}, // parts of the sound arrive up to 3 ms early or late
            {"Drift", 1.0f},        // each harmonic wanders up to 270 degrees either way
            {"Drift speed", 1.0f},  // a new place every frame: where the garble lives
            {"Spread", 0.0f},             // off: mono first
        };
        return info[i];
    }

    std::string displayValue(int i, float v) const override
    {
        if (i == Depth)
            return fmt("%.1f ms", depthMs(v));
        if (i == Drift)
            return fmt("%.0f deg", driftReach(v) * 180.0 / M_PI);
        if (i == DriftSpeed)
            return driftSeconds(v) >= 1.0 ? fmt("%.1f s", driftSeconds(v)) : fmt("%.0f ms", 1000.0 * driftSeconds(v));
        if (i == Spread)
            return v <= 0.0f ? std::string("Mono") : fmt("%.1f%% alike", 100.0 * alikeFloor(v));
        return Numin::displayValue(i, v);
    }

    void prepare(double newSampleRate) override
    {
        ear.prepare(newSampleRate, kite::kFrameSeconds);
        follower.prepare(newSampleRate);
        pitch.prepare(newSampleRate);
        spectrum.prepare(newSampleRate);
        body.prepare(spectrum.size() / 2, newSampleRate / spectrum.size(), spectrum.hopSamples() / newSampleRate);

        latency = spectrum.latencySamples();
        delaySide.prepare(latency);

        Numin::prepare(newSampleRate); // stores sampleRate, then calls reset()
    }

    int latencySamples() const override { return latency; }

    void reset() override
    {
        ear.reset();
        follower.reset();
        pitch.reset();
        spectrum.reset();
        body.reset();
        delaySide.reset();
        samplesIn = 0;

        depthNow = depthMs(params[Depth]);
        driftNow = driftReach(params[Drift]);
        speedNow = driftSeconds(params[DriftSpeed]);
        maxTurnNow = kite::Fan::maxTurnFor(alikeFloor(params[Spread]));
        voiced = 0.0;
        recent.fill({});
        hops = 0;
        lastHeardDb = -120.0;
    }

    // ---- meters ----------------------------------------------------------------------------
    // The sound's loudness as Kite hears it, and a shade under the moments it hears as clearly
    // pitched (only those drift and spread). Both describe the audio coming out now.
    enum Meter { InputMeter, PitchedMeter, NumMeters };
    int numMeters() const override { return NumMeters; }
    MeterInfo meterInfo(int i) const override
    {
        switch (i)
        {
        case InputMeter: return {"In", MeterKind::Trace};
        default: return {"Pitched", MeterKind::Shade};
        }
    }
    float meterValue(int i) const override
    {
        switch (i)
        {
        case InputMeter: return (float)lastHeardDb;
        default: return playingNow().voiced > 0.5 ? (float)lastHeardDb : -200.0f;
        }
    }
    void meterRangeDb(float& low, float& high) const override
    {
        low = -70.0f;
        high = -10.0f;
    }

    void process(const float* inL, const float* inR, float* outL, float* outR, int frames) override
    {
        const double turnTarget = kite::Fan::maxTurnFor(alikeFloor(params[Spread]));
        const double knobCoef = 1.0 - std::exp(-1.0 / (kKnobSmoothSeconds * sampleRate));
        const double depthCoef = 1.0 - std::exp(-1.0 / (kDepthSmoothSeconds * sampleRate));
        const double depthTarget = depthMs(params[Depth]);
        const double binHz = sampleRate / spectrum.size();

        for (int n = 0; n < frames; ++n)
        {
            const double mid = 0.5 * (inL[n] + inR[n]), side = 0.5 * (inL[n] - inR[n]);
            delaySide.write(side);
            depthNow += (depthTarget - depthNow) * depthCoef;
            driftNow += (driftReach(params[Drift]) - driftNow) * knobCoef;
            speedNow += (driftSeconds(params[DriftSpeed]) - speedNow) * knobCoef;
            maxTurnNow += (turnTarget - maxTurnNow) * knobCoef;

            // Hear: how loud the centre sounds (for the meter).
            if (ear.process(mid, mid))
                follower.addFrame(ear.intensity());

            // See: the centre in frequency, and what it's singing.
            pitch.push(mid);
            if (spectrum.push(mid))
                shapeFrame(binHz);

            // Two ears out: the sound, turned for each ear, plus whatever was already on the sides.
            double left, right;
            spectrum.pull(left, right);
            const double sides = delaySide.read(latency);
            outL[n] = (float)(left + sides);
            outR[n] = (float)(right - sides);

            ++samplesIn;
            lastHeardDb = follower.loudnessAt(samplesIn - 1 - latency);
        }
    }

  private:
    // A new frame is ready: give it body, then spread.
    void shapeFrame(double binHz)
    {
        const dsp::Pitch::Estimate heard = pitch.analyse();
        voiced += (heard.confidence - voiced) * kVoicedSmoothing;

        const double amount = 1.0; // always fully on
        const int bins = spectrum.size() / 2;
        std::fill(spectrum.sharedTurns(), spectrum.sharedTurns() + bins, 0.0);
        body.shape(spectrum.sharedTurns(), bins, binHz, heard.hz, voiced, amount * depthNow, amount * driftNow, speedNow);
        kite::Fan::shape(spectrum.splitTurns(), bins, binHz, heard.hz, voiced, amount * maxTurnNow);
        spectrum.render();

        recent[(size_t)(hops++ % kRecent)] = {voiced, amount};
    }

    // What the frame centred on the sample playing now decided. Frame centres run half a frame
    // ahead of what's playing, which is two hops.
    struct Decision
    {
        double voiced = 0.0, amount = 0.0;
    };
    Decision playingNow() const { return hops > 2 ? recent[(size_t)((hops - 3) % kRecent)] : Decision{}; }

    // ---- constants ---------------------------------------------------------------------------

    // How quickly Kite changes its mind about whether the sound is pitched (per frame, ~11 ms):
    // enough to ignore one doubtful frame, quick enough to follow a phrase starting and ending.
    static constexpr double kVoicedSmoothing = 0.4;

    static constexpr double kKnobSmoothSeconds = 0.020;

    // Depth moves slowly when you turn it: changing arrival times quickly would bend the pitch
    // (like a tape speeding up), and slowly it's inaudible.
    static constexpr double kDepthSmoothSeconds = 0.3;
    static constexpr int kRecent = 4;

    // ---- knobs -------------------------------------------------------------------------------

    // Depth: the most any part of the sound arrives early or late.
    static double depthMs(float v) { return 9.0 * v; } // 0 .. 9 ms

    // Drift: how far each harmonic may wander either way, in radians.
    static double driftReach(float v) { return 1.5 * M_PI * v; } // 0 .. 270 degrees

    // Drift speed: how long a harmonic takes to wander about a radian. From a slow breath to a new
    // place every frame (grain), evenly on a log scale.
    static double driftSeconds(float v) { return 3.0 * std::pow(0.01 / 3.0, v); } // 3 s .. 10 ms

    // Spread: how much wider the sound gets between the ears, as a floor on how alike they stay.
    // 1 = identical (no spread at all). On headphones the sweet spot is around 99% to 96% alike;
    // below that it starts to sound phasey, like two ears instead of one voice. The square gives
    // most of the knob to that sweet spot, and leaves the top of it for finding the edge.
    static double alikeFloor(float v) { return 1.0 - 0.08 * v * v; } // 100% .. 98% (half) .. 92% alike

    // ---- state -------------------------------------------------------------------------------

    int latency = 1;
    dsp::Ear ear;
    kite::Follower follower;
    dsp::Pitch pitch;
    dsp::Spectrum spectrum;
    kite::Body body;
    dsp::DelayLine delaySide;
    int64_t samplesIn = 0;

    double depthNow = 0.0, driftNow = 0.0, speedNow = 0.3, maxTurnNow = 0.0; // the knobs, smoothed so they never click
    double voiced = 0.0;                        // how surely the voice is pitched, smoothed
    std::array<Decision, kRecent> recent{};      // the last few frames' decisions, for the meters
    int64_t hops = 0;
    double lastHeardDb = -120.0;
};

} // namespace numinos

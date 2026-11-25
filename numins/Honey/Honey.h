// Honey — makes a vocal feel controlled without taking away its emotion.
//
// Every moment of the vocal should be well predicted by the moments around it.
// Honey listens ahead (about 2 s), and:
//   1. measures perceived loudness (K-weighted) every 5 ms "frame",
//   2. cuts the vocal into segments at the loudness valleys between syllables and words,
//   3. rates each segment and predicts it from its neighbours,
//   4. cuts any segment that rises above that prediction (Viscosity sets how hard),
//   5. moves the gain toward each segment's cut like a body with inertia: it starts
//      early and imperceptibly, glides, and can never move fast enough to be heard.
//      Mix-only: the latency is large.
//
// The stages live in their own files:
//   Segmenter.h     — seeing: loudness, noise floor, seams, rating        (steps 1–3)
//   Solver.h        — judging and deciding: prediction and the cuts       (steps 3–4)
//   GainFollower.h  — getting there: the inertial gain                    (step 5)
//   Segment.h       — the Segment they all share
// This file wires them together and owns the knob, the delay and playback.
#pragma once

#include "../Numin.h"
#include "../dsp/DelayLine.h"
#include "../dsp/KWeighting.h"

#include "GainFollower.h"
#include "Segment.h"
#include "Segmenter.h"
#include "Solver.h"

#include <cstdint>

namespace numinos
{

class Honey : public Numin
{
  public:
    enum Param { Viscosity, Output, NoiseFloor, NumParams };

    const char* name() const override { return "Honey"; }
    const char* category() const override { return "Dynamics"; }
    const char* blurb() const override { return "Transparently control perceived variation in perceived loudness over time."; }

    int numParams() const override { return NumParams; }
    ParamInfo paramInfo(int i) const override
    {
        static const ParamInfo info[NumParams] = {
            {"Viscosity", 0.5f, kMusicalEnd}, // the mark: where the musical range ends
            {"Output", 0.5f}, // 0 dB
            {"Noise floor", 0.0f}, // Auto
        };
        return info[i];
    }

    std::string displayValue(int i, float v) const override
    {
        if (i == Output)
            return fmt("%+.1f dB", outputDb(v));
        if (i == NoiseFloor)
            return v <= 0.0f ? std::string("Auto") : fmt("%.0f dB", noiseFloorDb(v));
        return Numin::displayValue(i, v);
    }

    void prepare(double newSampleRate) override
    {
        lookahead = (int)std::lround(kLookaheadSeconds * newSampleRate);

        weightL.prepare(newSampleRate);
        weightR.prepare(newSampleRate);
        delayL.prepare(lookahead);
        delayR.prepare(lookahead);
        segmenter.prepare(newSampleRate);

        Numin::prepare(newSampleRate); // stores sampleRate, then calls reset()
    }

    // ---- meters ------------------------------------------------------------------------
    // The loudness going in and coming out (the gap between them is Honey's gain), the noise
    // floor, everything Honey treats as silence, and Honey's own gain for the needle (without the
    // Output trim: it shows what Honey is doing, not where you've set the level). All describe the audio coming out right now
    // (the analysis runs `lookahead` ahead), so the meter lines up with what you hear.
    enum Meter { InputMeter, OutputMeter, NoiseFloorMeter, SilenceMeter, GainMeter, NumMeters };
    int numMeters() const override { return NumMeters; }
    MeterInfo meterInfo(int i) const override
    {
        switch (i)
        {
        case InputMeter: return {"In", MeterKind::Trace};
        case OutputMeter: return {"Out", MeterKind::Trace};
        case NoiseFloorMeter: return {"Noise floor", MeterKind::Line};
        case SilenceMeter: return {"Silence", MeterKind::Shade};
        default: return {"Gain", MeterKind::Gain};
        }
    }
    float meterValue(int i) const override
    {
        const double in = segmenter.loudnessAtFrameDb((samplesIn - lookahead) / segmenter.samplesPerFrame());
        switch (i)
        {
        case InputMeter: return (float)in;
        case OutputMeter: return (float)(in + lastGainDb);
        case NoiseFloorMeter: return (float)segmenter.noiseFloorInUseDb();
        case SilenceMeter: return (float)segmenter.silenceThresholdDb();
        default: return (float)(lastGainDb - trimDb);
        }
    }
    void meterRangeDb(float& low, float& high) const override
    {
        low = -90.0f;
        high = -10.0f;
    }

    // Honey plays its audio `lookahead` samples late; the host uses this to compensate.
    int latencySamples() const override { return lookahead; }

    void reset() override
    {
        weightL.reset();
        weightR.reset();
        delayL.reset();
        delayR.reset();
        segmenter.reset();
        gain.reset(baselineDb(params[Viscosity]));

        segments.clear();
        playIndex = 0;
        samplesIn = 0;
        trimDb = outputDb(params[Output]);
        lastGainDb = 0.0;
    }

    void process(const float* inL, const float* inR, float* outL, float* outR, int frames) override
    {
        // Viscosity also sets how quickly the gain can move: lighter, so quicker, as it rises.
        const double inertia = inertiaSeconds(params[Viscosity]);
        const double releaseInertia = releaseInertiaSeconds(params[Viscosity]);
        const int anticipation = (int)std::lround(kAnticipationRatio * inertia * sampleRate);
        const int cutAnticipation = (int)std::lround(std::min(kCutAnticipationRatio * inertia, kMaxCutAnticipationSeconds) * sampleRate);

        // Density: every word (and every gap) sits at this small attenuation unless it stands out.
        // Buried words are attenuated less, so they rise toward 0 dB. It's added back at the
        // output, so normal words and gaps come out at their original level.
        const double baseline = baselineDb(params[Viscosity]);

        // Output trim, smoothed so moving or automating it never clicks.
        const double trimTargetDb = outputDb(params[Output]);
        const double trimCoef = 1.0 - std::exp(-1.0 / (kTrimSmoothSeconds * sampleRate));

        // Where the noise sits: tracked automatically, or set by hand.
        segmenter.setNoiseFloorDb(params[NoiseFloor] <= 0.0f ? NAN : noiseFloorDb(params[NoiseFloor]));
        segmenter.setValleyDepthDb(valleyDepthDb(params[Viscosity]));

        for (int n = 0; n < frames; ++n)
        {
            const double l = inL[n], r = inR[n];
            delayL.write(l);
            delayR.write(r);

            // See: K-weighted power into the segmenter. Whenever it finishes a segment,
            // judge and decide the cuts again with the new context.
            const double kl = weightL.process(l), kr = weightR.process(r);
            if (segmenter.process(0.5 * (kl * kl + kr * kr), segments))
                honey::Solver::solve(segments, {viscosityWeight(params[Viscosity]), contextSeconds(params[Viscosity]),
                                                baseline, liftShare(params[Viscosity]), gather(params[Viscosity]),
                                                smoothness(params[Viscosity])});

            // Get there: the audio from `lookahead` samples ago. The gain chases the target from
            // `anticipation` samples in the future, so each glide is centred on its seam.
            const int64_t outSample = samplesIn - lookahead;
            // Cuts arrive early (seen from further ahead), releases stay centred: chase the lower target.
            const double cutTarget = targetGainDb(outSample + cutAnticipation);
            const double target = std::min(cutTarget, targetGainDb(outSample + anticipation));
            const double gainDb = gain.process(target, inertia, releaseInertia, sampleRate) - baseline; // baseline added back
            ++samplesIn;

            trimDb += (trimTargetDb - trimDb) * trimCoef;
            lastGainDb = gainDb + trimDb;
            const double g = dbToGain(lastGainDb);
            outL[n] = (float)(delayL.read(lookahead) * g);
            outR[n] = (float)(delayR.read(lookahead) * g);
        }
    }

  private:
    // ---- playback -----------------------------------------------------------------

    // Where the gain should be (dB) at `sample`: its segment's cut. In a gap, the nearest
    // segment's cut is held for kGapHoldFrames, so the glides happen in the gap, not the voice.
    double targetGainDb(int64_t sample)
    {
        const double baseline = baselineDb(params[Viscosity]);
        if (sample < 0)
            return baseline;
        const int64_t f = sample / segmenter.samplesPerFrame();

        // Forget segments that have finished, keeping enough as context for the solver.
        while (playIndex < segments.size() && segments[playIndex].end + kGapHoldFrames <= f)
            ++playIndex;
        while (playIndex > honey::Solver::kMaxNeighbours)
        {
            segments.popFront();
            --playIndex;
        }

        double gapGain = baseline; // gaps sit at the baseline, so noise is never raised
        for (int i = playIndex; i < segments.size() && segments[i].start - kGapHoldFrames <= f; ++i)
        {
            honey::Segment& s = segments[i];
            s.locked = true; // it's playing (or about to): its gain is final
            if (f >= s.start && f < s.end)
                return s.gainDb;
            if (i == playIndex)
                gapGain = s.gainDb;
        }
        return gapGain;
    }

    // ---- constants ----------------------------------------------------------------

    // The lookahead budget. A segment must be fully decided before it starts playing, so this must
    // cover: the longest segment (Segmenter::kMaxSegmentFrames, 0.3 s) + the time to confirm its
    // valley (~165 ms) + the gap hold (100 ms) + the furthest read-ahead (~170 ms), with margin.
    // Keep it under ~1 s: Logic doesn't compensate a single plugin's latency beyond about that.
    // If you raise any of those parts, raise this with them.
    static constexpr double kLookaheadSeconds = 0.8;

    // where each move is "centered" relative to its seam. at higher values it's more Brennan Mono Rail like. The gain attenuation
    // will start earlier so that more of it is in place before a peak and the curve is smoother, but this does come at the cost
    // of this attenuatio happening at the end of the word before. another way to think about it: when is a move halfway-done by?
    // i may experiment with how much we can get away with here. on paper we could be heavily pre-firing the attenuation, but i'm
    // suspicious that our ears won't pick up on that as fast, so we may be able to get the upside of faster attenuation without feeling
    // like a previous segment has been robbed. more to come. 1.68 is centered.
    static constexpr double kAnticipationRatio = 2.68;

    // how far ahead cuts are seen: at 4 inertia times, a cut is ~90% in place when its word begins
    static constexpr double kCutAnticipationRatio = 4.0;
    static constexpr double kMaxCutAnticipationSeconds = 0.15; // part of the lookahead budget above

    // ---- Viscosity: a musical range, then a push range ---------------------------------
    // 0 .. kMusicalEnd is the musical range: transparent, getting away with it. Every mapping
    // below reaches its "musical" maximum at the mark. Beyond it, Honey pushes: stronger, quicker,
    // wider, finer, more lift, and it starts to gather the vocal toward the level of the line,
    // rather than only smoothing out what stands out. It's allowed to have a sound up there.
    static constexpr float kMusicalEnd = 0.8f;
    static float musical(float v) { return std::min(1.0f, v / kMusicalEnd); }                // 0 .. 1 by the mark
    static float push(float v) { return std::max(0.0f, (v - kMusicalEnd) / (1.0f - kMusicalEnd)); } // 0 .. 1 beyond it

    // this is how quickly the gain can move. in practice this decides how much of a planned cut is actually realized on a short syllable
    // initially set to 0.100
    static double inertiaSeconds(float v) { return 0.200 * std::pow(0.25, musical(v)) * std::pow(0.6, push(v)); } // 200 .. 50 ms (mark) .. 30 ms

    // Releases don't get quicker in the push range: cuts can arrive faster up there, but the way
    // back out stays as unhurried as it is at the mark. Getting away with it happens on the way out.
    static double releaseInertiaSeconds(float v) { return 0.200 * std::pow(0.25, musical(v)); } // 200 .. 50 ms (mark), then held

    // hold a segment's gain 150 ms into a gap. this is to safeguard against that "pumping" problem from earlier when
    // gain recovers too quickly at the tail of a word. it's jarring.
    static constexpr int kGapHoldFrames = 20;

    // loud and fast spikes in sound are surprises. this control sort of decides how much
    // of a given surprise we should remove. At 10 we're already looking at around 91% though,
    // so this lever is effectively used up. may need to rework some things.
    static double viscosityWeight(float v) { return 0.1 * std::pow(300.0, musical(v)) * std::pow(3.0, push(v)); } // 0.1 .. 30 (mark) .. 90

    // how far Honey looks when judging a word: from ~0.7 s (single words that jump out of their
    // neighbours) to 3 s (runs of loud words that jump out of the line they're in).
    static double contextSeconds(float v) { return 0.7 * std::pow(3.0 / 0.7, musical(v)) * std::pow(2.0, push(v)); } // 0.7 .. 3 s (mark) .. 6 s

    // How much density Honey adds: the baseline attenuation every word and gap sits at, and so
    // the most a buried word can be lifted. Grows with Viscosity, like everything else.
    static double baselineDb(float v) { return -(kMaxBaselineDb * musical(v) + kPushBaselineDb * push(v)); } // 0 .. -1.8 (mark) .. -3.5 dB
    static constexpr double kMaxBaselineDb = 1.8, kPushBaselineDb = 1.7;

    // How strongly buried words are lifted, relative to how strongly loud ones are cut. Lifting is
    // deliberately gentler: it should read as density, never as everything being turned up.
    // I'm still fine-tuning this one quite a bit as there are a few different variables that altogether
    // have an effect on the "gaining up" of quiet parts as our ear might hear it. this is one lever.
    // (In the push range it grows, as Honey starts gathering the vocal together.)
    static double liftShare(float v) { return 0.15 + 0.45 * push(v); } // 0.15 .. 0.6

    // Push range only: how far the reference moves from "what my neighbours predict" toward "the
    // level of the line I'm in". At 0 Honey smooths surprises; toward 1 it pulls the vocal together.
    static double gather(float v) { return 0.7 * push(v); } // 0 (mark) .. 0.7

    // Push range only: finer segments, so syllables inside words can be handled on their own.
    static double valleyDepthDb(float v) { return 3.0 - 1.0 * push(v); } // 3 dB (mark) .. 2 dB

    // How strongly neighbouring segments of a phrase are kept at similar gains (relative to lambda).
    // This is what lets Honey do more without reshaping the envelope: it levels stretches of a
    // line together instead of pushing syllables up and down against each other.
    static double smoothness(float v) { return kSmoothness * (0.5 + 0.5 * musical(v)); }
    static constexpr double kSmoothness = 1.0;

    // Where the noise sits. At 0 it's "Auto": Honey finds the noise floor itself. Above 0 you set
    // it: put it where the noise is (the bottom of the valleys on the meter). Honey treats the
    // noise, and the wobble just above it, as silence, exactly as Auto does. K-weighted dBFS, so a
    // little different from a plain meter. Useful for testing, since it never shifts with playback.
    static double noiseFloorDb(float v) { return -90.0 + 60.0 * v; } // -90 .. -30 dB

    // Output trim: Honey only cuts, so this lets you level-match against bypass.
    static double outputDb(float v) { return -12.0 + 24.0 * v; } // -12 .. +12 dB
    static constexpr double kTrimSmoothSeconds = 0.020;

    // ---- state --------------------------------------------------------------------

    int lookahead = 1;
    dsp::KWeighting weightL, weightR;
    dsp::DelayLine delayL, delayR;

    honey::Segmenter segmenter;
    honey::SegmentList segments;
    honey::GainFollower gain;
    double trimDb = 0.0;
    double lastGainDb = 0.0; // the net gain on the latest sample, for the meter

    int playIndex = 0;
    int64_t samplesIn = 0;
};

} // namespace numinos

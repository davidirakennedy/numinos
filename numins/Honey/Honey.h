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
    enum Param { Viscosity, NumParams };

    const char* name() const override { return "Honey"; }
    const char* category() const override { return "Dynamics"; }
    const char* blurb() const override { return "Nothing jumps out; the performance stays."; }

    int numParams() const override { return NumParams; }
    ParamInfo paramInfo(int i) const override
    {
        static const ParamInfo info[NumParams] = {
            {"Viscosity", 0.5f},
        };
        return info[i];
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

    // Honey plays its audio `lookahead` samples late; the host uses this to compensate.
    int latencySamples() const override { return lookahead; }

    void reset() override
    {
        weightL.reset();
        weightR.reset();
        delayL.reset();
        delayR.reset();
        segmenter.reset();
        gain.reset();

        segments.clear();
        playIndex = 0;
        samplesIn = 0;
    }

    void process(const float* inL, const float* inR, float* outL, float* outR, int frames) override
    {
        // Viscosity also sets how quickly the gain can move: lighter, so quicker, as it rises.
        const double inertia = inertiaSeconds(params[Viscosity]);
        const int anticipation = (int)std::lround(kAnticipationRatio * inertia * sampleRate);
        const int cutAnticipation = (int)std::lround(std::min(kCutAnticipationRatio * inertia, kMaxCutAnticipationSeconds) * sampleRate);

        for (int n = 0; n < frames; ++n)
        {
            const double l = inL[n], r = inR[n];
            delayL.write(l);
            delayR.write(r);

            // See: K-weighted power into the segmenter. Whenever it finishes a segment,
            // judge and decide the cuts again with the new context.
            const double kl = weightL.process(l), kr = weightR.process(r);
            if (segmenter.process(0.5 * (kl * kl + kr * kr), segments))
                honey::Solver::solve(segments, viscosityWeight(params[Viscosity]), contextSeconds(params[Viscosity]));

            // Get there: the audio from `lookahead` samples ago. The gain chases the target from
            // `anticipation` samples in the future, so each glide is centred on its seam.
            const int64_t outSample = samplesIn - lookahead;
            // Cuts arrive early (seen from further ahead), releases stay centred: chase the lower target.
            const double cutTarget = targetGainDb(outSample + cutAnticipation);
            const double target = std::min(cutTarget, targetGainDb(outSample + anticipation));
            const double gainDb = gain.process(target, inertia, sampleRate);
            ++samplesIn;

            const double g = dbToGain(gainDb);
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
        if (sample < 0)
            return 0.0;
        const int64_t f = sample / segmenter.samplesPerFrame();

        // Forget segments that have finished, keeping enough as context for the solver.
        while (playIndex < segments.size() && segments[playIndex].end + kGapHoldFrames <= f)
            ++playIndex;
        while (playIndex > honey::Solver::kMaxNeighbours)
        {
            segments.popFront();
            --playIndex;
        }

        double gapGain = 0.0;
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
    // like a previous segment has been robbed. more to come.
    static constexpr double kAnticipationRatio = 1.68;

    // how far ahead cuts are seen: at 4 inertia times, a cut is ~90% in place when its word begins
    static constexpr double kCutAnticipationRatio = 4.0;
    static constexpr double kMaxCutAnticipationSeconds = 0.15; // part of the lookahead budget above

    // this is how quickly the gain can move. in practice this decides how much of a planned cut is actually realized on a short syllable
    static double inertiaSeconds(float v) { return 0.100 * std::pow(0.25, v); }  // 100 ms .. 25 ms

    // hold a segment's gain 150 ms into a gap. this is to safeguard against that "pumping" problem from earlier when
    // gain recovers too quickly at the tail of a word. it's jarring.
    static constexpr int kGapHoldFrames = 20;

    // loud and fast spikes in sound are surprises. this control sort of decides how much
    // of a given surprise we should remove. At 10 we're already looking at around 91% though,
    // so this lever is effectively used up. may need to rework some things.
    static double viscosityWeight(float v) { return 0.1 * std::pow(100.0, v); } // 0.1 .. 10

    // how far Honey looks when judging a word: from ~0.7 s (single words that jump out of their
    // neighbours) to 3 s (runs of loud words that jump out of the line they're in).
    static double contextSeconds(float v) { return 0.7 * std::pow(3.0 / 0.7, v); } // 0.7 s .. 3 s

    // ---- state --------------------------------------------------------------------

    int lookahead = 1;
    dsp::KWeighting weightL, weightR;
    dsp::DelayLine delayL, delayR;

    honey::Segmenter segmenter;
    honey::SegmentList segments;
    honey::GainFollower gain;

    int playIndex = 0;
    int64_t samplesIn = 0;
};

} // namespace numinos

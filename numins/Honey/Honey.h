// Honey — makes a vocal feel controlled without taking away its emotion.
//
// Every moment of the vocal should be well predicted by the moments around it.
// Honey listens ahead (0.8 s), and:
//   1. hears how loud every 5 ms "frame" sounds, and how vowel-like it is,
//   2. cuts the vocal into segments at the loudness valleys between syllables and words,
//   3. rates each segment by its vowel and predicts it from its neighbours,
//   4. decides a level for every segment: cut if it jumps out, a little less if it's buried
//      (Viscosity sets how hard),
//   5. plans the gain as one smooth curve through those levels, moving where it can't be
//      heard: in valleys, in fading tails, across gaps. Re-planned every frame.
//      Mix-only: the latency is large.
//
// The stages live in their own files:
//   ../dsp/Ear.h    — hearing: perceived loudness and vowel-ness per frame (step 1, shared)
//   Segmenter.h     — seeing: noise floor, seams, rating                   (steps 2–3)
//   Solver.h        — judging and deciding: prediction and the levels      (steps 3–4)
//   GainPath.h      — moving: the planned gain curve                       (step 5)
//   Segment.h       — the Segment they all share
// This file wires them together and owns the knob, the delay and playback.
#pragma once

#include "../Numin.h"
#include "../dsp/DelayLine.h"
#include "../dsp/Ear.h"

#include "GainPath.h"
#include "Segment.h"
#include "Segmenter.h"
#include "Solver.h"

#include <algorithm>
#include <array>
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

        ear.prepare(newSampleRate, honey::kFrameSeconds);
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
        const double in = segmenter.loudnessAtFrameDb((samplesIn - lookahead) / ear.samplesPerFrame());
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
        ear.reset();
        delayL.reset();
        delayR.reset();
        segmenter.reset();
        path.reset(baselineDb(params[Viscosity]));

        segments.clear();
        playIndex = 0;
        samplesIn = 0;
        trimDb = outputDb(params[Output]);
        lastGainDb = 0.0;
    }

    void process(const float* inL, const float* inR, float* outL, float* outR, int frames) override
    {
        // Viscosity also sets how readily the gain moves: lighter, so quicker, as it rises.
        const honey::GainPath::Settings pathSettings = pathSettingsFor(params[Viscosity]);
        const int frameLength = ear.samplesPerFrame();

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

            // Hear and see: every frame the Ear hears goes to the Segmenter. Whenever it finishes a
            // segment, judge and decide the levels again with the new context.
            if (ear.process(l, r) && segmenter.addFrame(ear.intensity(), ear.vowel(), segments))
                honey::Solver::solve(segments, {viscosityWeight(params[Viscosity]), contextSeconds(params[Viscosity]),
                                                baseline, liftShare(params[Viscosity]), gather(params[Viscosity]),
                                                smoothness(params[Viscosity]), smoothKneeDb(params[Viscosity]),
                                                kSeamCost});

            // Move: we play the audio from `lookahead` samples ago. At the start of each of its frames,
            // the gain curve is re-planned from where it is now to the end of what Honey can see;
            // in between, the gain runs straight from one planned point to the next.
            const int64_t outSample = samplesIn - lookahead;
            if (outSample >= 0 && outSample % frameLength == 0)
            {
                path.advance();
                planPath(outSample / frameLength, pathSettings, params[Viscosity]);
            }
            const double through = outSample >= 0 ? (double)(outSample % frameLength) / frameLength : 0.0;
            const double gainDb = path.gainAt(through) - baseline; // baseline added back
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

    // Forget segments that have finished (keeping enough behind as the solver's context), and lock
    // the ones about to play: from kLockFrames before a segment starts, its level is final, so the
    // path can commit to it.
    void updateSegments(int64_t now)
    {
        while (playIndex < segments.size() && segments[playIndex].end <= now)
            ++playIndex;
        while (playIndex > honey::Solver::kMaxNeighbours)
        {
            segments.popFront();
            --playIndex;
        }
        for (int i = playIndex; i < segments.size() && segments[i].start <= now + kLockFrames; ++i)
            segments[i].locked = true;
    }

    // Describe every frame from the one after `now` to the newest Honey can see, and plan the gain
    // path through them. For each frame: the level it should be at, how much being there matters,
    // and how audible a gain change would be there (Segmenter::audibility, the heart of it).
    //   * In a segment: the segment's level. Being there matters in the same proportion as
    //     audibility (never less than kMinHold), so the curve hits each level on the vowel and is
    //     free to travel in between.
    //   * In a trail-off (a phrase fading into a rest): kept present (see TrailOff below).
    //   * In a gap: the level it was left at. Gaps continue, they don't reset: across a rest the
    //     gain glides straight from one line's level to the next, never back up to the baseline in
    //     between (which made a rise in the rest, then a swing down into the next line).
    void planPath(int64_t now, const honey::GainPath::Settings& settings, float v)
    {
        updateSegments(now);
        const int64_t newest = segmenter.newestLoudnessFrame();
        const int n = (int)std::clamp<int64_t>(newest - now, 0, honey::GainPath::kMaxFrames);
        findTrailOffs();

        const double share = presenceShare(v), most = presenceMaxDb(v);
        const double presenceFloor = segmenter.noiseFloorInUseDb() + kPresenceFloorMarginDb;
        double carried = path.currentDb(); // what a gap continues
        int k = playIndex;                 // the segment at (or after) the frame being described
        int t = 0;                         // the trail-off at (or after) it
        for (int i = 0; i < n; ++i)
        {
            const int64_t f = now + 1 + i;
            while (k < segments.size() && segments[k].end <= f)
                ++k;
            while (t < numTrailOffs && trailOffs[(size_t)t].to <= f)
                ++t;
            const bool inSegment = k < segments.size() && segments[k].start <= f;
            const double importance = segmenter.audibility(f);

            honey::GainPath::Frame frame{carried, 0.0, importance}; // a gap: continue
            if (t < numTrailOffs && trailOffs[(size_t)t].from <= f)
            {
                TrailOff& tail = trailOffs[(size_t)t];
                for (int64_t g = std::max(tail.from, tail.seen + 1); g <= f; ++g) // include any of it already played
                {
                    tail.lowestDb = std::min(tail.lowestDb, segmenter.loudnessAtFrameDb(g));
                    if (tail.lowestDb >= presenceFloor) // only while it's clearly voice; then the lift holds
                        tail.liftDb = std::min(most, share * (tail.peakDb - tail.lowestDb));
                }
                tail.seen = f;
                frame = {tail.levelDb + tail.liftDb, std::max(kTrailHold, importance), importance};
            }
            else if (inSegment)
                frame = {segments[k].gainDb, std::max(kMinHold, importance), importance};

            pathFrames[(size_t)i] = frame;
            carried = frame.targetDb;
        }
        path.replan(pathFrames.data(), n, settings);
    }

    // ---- presence ------------------------------------------------------------------
    //
    // A phrase fading into a rest isn't a surprise, but when it falls away too fast the closeness of
    // the voice breaks. So Honey keeps trail-offs present, gently, by one rule: it may slow the
    // fall, never reverse it. From the loudest moment of the phrase's last word to the end of its
    // tail, the gain rises by a share of how far the voice has fallen since that moment (measured on
    // the lowest point so far, so the lift only ever grows), up to a cap. The tail still fades the
    // whole way down, just more slowly. Because the lift comes from the fall itself it can't arrive
    // early or late, or grow faster than the tail fades: no swell, no crescendo. And it stops growing
    // once the tail nears the noise floor, so breaths and room tone are never brought forward.
    // A little of this also makes the cuts elsewhere more transparent: a vocal that's only ever
    // pulled down starts to sound like it's being pulled down.
    struct TrailOff
    {
        int64_t from = 0, to = 0; // the word's loudest frame .. the end of its tail
        double peakDb = 0.0;      // loudness at that moment
        double levelDb = 0.0;     // the word's level (the Solver's), which the lift is added to
        double lowestDb = 0.0;    // the lowest the tail has been so far (while planning)
        double liftDb = 0.0;      // the lift so far (while planning)
        int64_t seen = -1;        // the last frame folded into lowestDb and liftDb
    };

    // Find the trail-offs in view: every segment with a rest after it (kRestFrames of silence, or
    // nothing decided after it for that long), traced back over the followers touching it (its tail
    // pieces) to the word they belong to. A lone breath in a rest belongs to no word: nothing to keep.
    void findTrailOffs()
    {
        numTrailOffs = 0;
        const int64_t decided = segmenter.newestDecidedFrame();
        for (int j = std::max(0, playIndex - 1); j < segments.size() && numTrailOffs < kMaxTrailOffs; ++j)
        {
            const honey::Segment& last = segments[j];
            const int64_t restStart = last.end;
            const int64_t restEnd = j + 1 < segments.size() ? segments[j + 1].start : decided;
            if (restEnd - restStart < kRestFrames)
                continue;

            int w = j;
            while (w > 0 && segments[w].prominence < 0.5 && segments[w].start - segments[w - 1].end < kRestFrames)
                --w;
            const honey::Segment& word = segments[w];
            if (word.prominence < 0.5)
                continue;

            TrailOff tail;
            tail.from = word.start;
            for (int64_t f = word.start; f < word.end; ++f)
                if (segmenter.loudnessAtFrameDb(f) > segmenter.loudnessAtFrameDb(tail.from))
                    tail.from = f;
            tail.to = last.end;
            tail.peakDb = tail.lowestDb = segmenter.loudnessAtFrameDb(tail.from);
            tail.levelDb = word.gainDb;
            trailOffs[(size_t)numTrailOffs++] = tail;
        }
    }

    // The path's costs from Viscosity. The gain's "mass" is inertiaSeconds, in frames (m). Moving
    // on a vowel costs (kMotionScale * m)^2 per step and bending costs (kBendScale * m)^4: so a move
    // that has to cross a stretch of voice spreads over about kMotionScale * m frames, and every
    // corner is rounded over about kBendScale * m. (Each penalty is a length raised to the power of
    // how many neighbours it links, which is what makes those lengths come out in frames.)
    // Upward moves pay (releaseInertia / inertia)^2 extra, so recoveries are as unhurried as
    // releaseInertiaSeconds says.
    //
    // Measured on take.wav at the mark, against the old follower: the fastest gain move while a
    // vowel is at its height fell from ~41 to ~9 dB/s, and cuts are realised just as fully.
    static honey::GainPath::Settings pathSettingsFor(float v)
    {
        const double m = inertiaSeconds(v) / honey::kFrameSeconds;
        const double release = releaseInertiaSeconds(v) / inertiaSeconds(v);
        return {std::pow(kMotionScale * m, 2.0), std::pow(kBendScale * m, 4.0), release * release};
    }

    // ---- constants ----------------------------------------------------------------

    // The lookahead budget. A segment must be fully decided before it's locked, so this must cover:
    // the longest segment (Segmenter::kMaxSegmentFrames, 0.3 s) + the time to confirm its valley
    // (~165 ms) + the lock (kLockFrames, 150 ms), with margin. Whatever is left is how early the
    // path sees each decision, and so how early (and gently) it can start moving toward it.
    // Keep it under ~1 s: Logic doesn't compensate a single plugin's latency beyond about that.
    static constexpr double kLookaheadSeconds = 0.8;

    // A segment's level is final this long before it plays (frames; 30 = 150 ms).
    static constexpr int kLockFrames = 30;

    static constexpr double kMinHold = 0.05; // inside a segment, being at its level always matters this much

    // Presence (see TrailOff). A rest is at least kRestFrames (150 ms) of silence after a phrase.
    // In a trail-off, being on the presence curve matters at least kTrailHold, so the gain follows
    // the tail's own shape. The lift stops growing kPresenceFloorMarginDb above the noise floor.
    static constexpr int kRestFrames = 30;
    static constexpr double kTrailHold = 0.3;
    static constexpr double kPresenceFloorMarginDb = 18.0;
    static constexpr int kMaxTrailOffs = 16;

    // What a level difference between neighbouring segments costs to perform, relative to lambda,
    // at a seam where a move would be fully audible (see Solver.h, the performance cost). It's
    // priced by the seam's audibility, so across a silence it's ~free and inside a word it's dear.
    // Measured on take.wav at the mark: the average level difference across audible seams (mostly
    // the dips inside words) was 0.48 dB without it, 0.33 at 1, 0.19 at 4, with no change in how
    // much Honey levels overall. Most seams are well hidden (median audibility ~0.01), so this only
    // touches the few that aren't. Higher = a word's syllables are kept more firmly together.
    static constexpr double kSeamCost = 2.0;

    // The path's costs, relative to the gain's mass (see pathSettingsFor).
    static constexpr double kMotionScale = 2.0, kBendScale = 0.5;

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

    // Presence: how much of a trail-off's fall is given back (0.3 = it fades 70% as fast), and the
    // most it can be lifted. Grows with Viscosity like everything else; ever so gentle in the musical
    // range, a little more when pushing. Always less than the baseline's depth, so never above 0 dB.
    static double presenceShare(float v) { return 0.3 * musical(v) + 0.1 * push(v); }    // 0 .. 0.3 (mark) .. 0.4
    static double presenceMaxDb(float v) { return 1.5 * musical(v) + 1.0 * push(v); }    // 0 .. 1.5 dB (mark) .. 2.5 dB

    // Push range only: finer segments, so syllables inside words can be handled on their own.
    static double valleyDepthDb(float v) { return 3.0 - 1.0 * push(v); } // 3 dB (mark) .. 2 dB

    // How strongly neighbouring segments of a phrase are kept at similar gains (relative to lambda).
    // This is what lets Honey do more without reshaping the envelope: it levels stretches of a
    // line together instead of pushing syllables up and down against each other.
    static double smoothness(float v) { return kSmoothness * (0.5 + 0.5 * musical(v)); }
    static constexpr double kSmoothness = 1.0;

    // Where the glue between neighbours stops resisting harder (see Solver.h, glue): differences
    // between neighbours' gains smaller than this are held together, bigger ones are allowed, so a
    // word that really jumps out can be cut without the rest of the line rocking with it.
    static double smoothKneeDb(float v) { return 2.0 * std::pow(0.5, musical(v)) * std::pow(0.5, push(v)); } // 2 .. 1 dB (mark) .. 0.5 dB

    // Where the noise sits. At 0 it's "Auto": Honey finds the noise floor itself. Above 0 you set
    // it: put it where the noise is (the bottom of the valleys on the meter). Honey treats the
    // noise, and the wobble just above it, as silence, exactly as Auto does. In dB as Honey's Ear hears it, so a
    // little different from a plain meter. Useful for testing, since it never shifts with playback.
    static double noiseFloorDb(float v) { return -90.0 + 60.0 * v; } // -90 .. -30 dB

    // Output trim: Honey only cuts, so this lets you level-match against bypass.
    static double outputDb(float v) { return -12.0 + 24.0 * v; } // -12 .. +12 dB
    static constexpr double kTrimSmoothSeconds = 0.020;

    // ---- state --------------------------------------------------------------------

    int lookahead = 1;
    dsp::Ear ear;
    dsp::DelayLine delayL, delayR;

    honey::Segmenter segmenter;
    honey::SegmentList segments;
    honey::GainPath path;
    std::array<honey::GainPath::Frame, honey::GainPath::kMaxFrames> pathFrames{};
    std::array<TrailOff, kMaxTrailOffs> trailOffs{};
    int numTrailOffs = 0;
    double trimDb = 0.0;
    double lastGainDb = 0.0; // the net gain on the latest sample, for the meter

    int playIndex = 0;
    int64_t samplesIn = 0;
};

} // namespace numinos

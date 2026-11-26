// GainPath — Honey's "moving": the gain as one smooth curve, planned across the whole lookahead.
//
// The Solver decides what level each segment should sit at. Getting there used to be a separate
// step: a follower with inertia chased a staircase of targets, and a handful of tricks hid the
// steps (start cuts early, hold gains into gaps, shape releases, centre moves on their seams).
// They all approximated one idea: *move the gain where it can't be heard*. GainPath says that
// idea directly, as a cost, and finds the cheapest curve:
//
//   sum hold      * (gain - target)^2        be at each segment's level, above all on its vowel
//   + sum motion  * (gain - previous)^2      moving costs: a lot while the voice sounds,
//                                            almost nothing in a valley or a gap
//   + sum bending * (change in slope)^2      and never with a corner: every move is an S-curve
//
// one value per 5 ms frame, from the frame playing now to the end of what Honey can see. Because
// each term only links a frame to its neighbours, the best curve is the solution of a banded
// linear system (dsp::Pentadiagonal): exact, and cheap enough to redo every frame. So the curve is
// re-planned 200 times a second as new frames and decisions arrive, always starting from the gain
// that's actually playing, with its slope: it can change its mind, but never with a jump.
//
// What falls out, without being written anywhere: cuts happen in the valley before a word, not on
// its onset; releases happen in the fading tail, when the voice is quietest; and across a gap the
// gain glides from one phrase's level to the next over the whole silence, instead of sitting at
// one and dropping just before the next line.
#pragma once

#include "../dsp/Pentadiagonal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <utility>

namespace numinos::honey
{

class GainPath
{
  public:
    static constexpr int kMaxFrames = 256; // longer than the lookahead, in frames

    // One upcoming frame, as Honey describes it.
    struct Frame
    {
        double targetDb;   // where the gain should be (the segment's level, or a gap's resting level)
        double hold;       // how much being there matters (>= 0): high on a vowel, ~0 in a gap
        double importance; // how audible a gain change is here, 0..1: ~1 on a vowel, ~0 in silence
    };

    struct Settings
    {
        double motionWeight;  // cost of moving while the voice sounds (grows with the gain's "mass")
        double bendWeight;    // cost of a change in slope: how round every move is
        double releaseWeight; // extra factor on upward moves (> 1: recoveries are more unhurried)
    };

    // Start at rest at `startDb`.
    void reset(double startDb)
    {
        played = {startDb, startDb};
        plan.fill(startDb);
        planned = 0;
    }

    // A new frame begins: the knot we were heading for is now the one playing.
    void advance()
    {
        played[0] = played[1];
        played[1] = next();
    }

    // Plan the curve over `frames[0..n)`, the frames after the one now playing.
    void replan(const Frame* frames, int n, const Settings& s)
    {
        n = std::min(n, kMaxFrames);
        if (n <= 0)
        {
            planned = 0;
            return;
        }
        system.clear(n);

        // Which way each step went in the previous plan (shifted one frame along, since a frame
        // has passed). A quadratic cost can't tell up from down, so asymmetry comes from here:
        // steps that were rising pay the release factor. The plan barely changes frame to frame,
        // so this is stable.
        auto wasRising = [&](int i) { return i + 1 < planned && plan[(size_t)i + 1] > plan[(size_t)i]; };

        for (int i = 0; i < n; ++i)
        {
            const Frame& f = frames[i];
            addSquare(f.hold + kAnchor, {{i, 1.0}}, f.targetDb);

            const double audible = 0.5 * (f.importance + frames[std::max(0, i - 1)].importance);
            const double motion = s.motionWeight * (kFreeMotion + audible) * (wasRising(i) ? s.releaseWeight : 1.0);
            addSquare(motion, {{i, 1.0}, {i - 1, -1.0}}, 0.0);

            addSquare(s.bendWeight, {{i, 1.0}, {i - 1, -2.0}, {i - 2, 1.0}}, 0.0);
        }

        system.solve(plan.data());
        planned = n;

        // A last line of defence: if anything upstream ever hands us a bad number, the plan is
        // thrown away and the gain simply holds where it is, rather than a NaN reaching the audio.
        for (int i = 0; i < n; ++i)
            if (!std::isfinite(plan[(size_t)i]))
            {
                planned = 0;
                break;
            }
    }

    // The gain (dB) playing at the start of this frame.
    double currentDb() const { return played[1]; }

    // The gain (dB) a fraction 0..1 of the way through the frame now playing.
    double gainAt(double fraction) const { return played[1] + (next() - played[1]) * fraction; }

  private:
    // The knot at the end of the frame now playing. Never above 0 dB: Honey's gain is a cut.
    double next() const { return planned > 0 ? std::min(0.0, plan[0]) : played[1]; }

    // Add weight * (sum of coeff * x[index] - target)^2 to the cost. Indices -1 and -2 are the two
    // knots already played: they're known, so they move to the right-hand side.
    void addSquare(double weight, std::initializer_list<std::pair<int, double>> terms, double target)
    {
        if (weight <= 0.0)
            return;
        double known = 0.0;
        for (auto [index, coeff] : terms)
            if (index < 0)
                known += coeff * played[(size_t)(2 + index)];
        for (auto [i, ci] : terms)
        {
            if (i < 0)
                continue;
            system.addRhs(i, weight * ci * (target - known));
            for (auto [j, cj] : terms)
                if (j >= i)
                    system.addMatrix(i, j, weight * ci * cj);
        }
    }

    // A whisper of a pull toward each frame's target everywhere, so the curve is always fully
    // determined, even across a long silence. Far too small to hear.
    static constexpr double kAnchor = 1.0e-4;

    // Moving in silence still costs a little (relative to moving on a vowel), so a gap's glide
    // spreads out rather than happening all at once.
    static constexpr double kFreeMotion = 0.02;

    std::array<double, 2> played{};                  // the two knots already played: {before, now}
    std::array<double, kMaxFrames> plan{};           // the planned knots after them
    int planned = 0;
    dsp::Pentadiagonal<kMaxFrames> system;
};

} // namespace numinos::honey

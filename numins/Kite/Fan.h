// Fan — spread: the voice a little wider between the two ears, fanned out along its own harmonics.
//
// Each harmonic is given its own tiny lean, a little earlier in one ear than the other, some toward
// the left and some toward the right. The ear holds a voice's harmonics together as one sound
// extremely strongly (they share one pitch and move together), so it won't hear them as parts
// coming from different places. It hears one voice, a little wider.
//
// This is the stereo half (inherited from Bloom), on top of Body (which works in mono). On headphones it's sweet up to about 96% alike and phasey beyond.
//
//   * Only the voice's harmonics lean. Breath, sibilance and noise have nothing holding them
//     together, so spreading them would pull them apart: when the voice isn't clearly pitched,
//     nothing leans.
//   * The pattern follows the voice. Each lean belongs to a harmonic (the 5th, the 6th...), not to
//     a frequency, so when the pitch moves the whole fan moves with it and the image stays steady.
//   * The lowest harmonics stay centred. They anchor the voice in the middle, wherever its pitch is.
//   * One voice, always. A lean of angle a in each ear (opposite ways) leaves the ears exactly
//     cos(2a) alike at that harmonic, so capping the lean caps how different the ears can ever
//     be. Spread sets that cap as a floor on how alike they stay.
#pragma once

#include <algorithm>
#include <cmath>

namespace numinos::kite
{

struct Fan
{
    // How far the fan may open: the ears stay at least this alike (1 = identical).
    static double maxTurnFor(double alikeFloor) { return 0.5 * std::acos(std::clamp(alikeFloor, -1.0, 1.0)); }

    // Set the left ear's split turn for every bin (the right gets the opposite, in Spectrum).
    //   pitchHz, voiced: what the voice is singing, and how sure (0 .. 1)
    //   maxTurn:         the cap, from maxTurnFor()
    static void shape(double* turns, int numBins, double binHz, double pitchHz, double voiced, double maxTurn)
    {
        const double depth = maxTurn * voiced;
        for (int k = 0; k < numBins; ++k)
            turns[k] = 0.0;
        if (depth <= 0.0 || pitchHz <= 0.0)
            return;

        for (int k = 1; k < numBins; ++k)
        {
            const double hz = k * binHz;
            // Every bin belongs to its nearest harmonic, so a harmonic's whole peak turns together.
            const int harmonic = (int)std::lround(hz / pitchHz);
            turns[k] = depth * lean(harmonic) * anchor(harmonic) * topFade(hz);
        }
    }

  private:
    // Which way, and how far, each harmonic leans (-1 .. 1). Stepping around a circle by the golden
    // ratio: neighbours mostly lean opposite ways, and the pattern never repeats, so no stretch
    // of the voice ends up leaning one way.
    static double lean(int harmonic) { return std::sin(2.0 * M_PI * kGolden * harmonic); }

    // The lowest harmonics stay centred: none at the 1st and 2nd, full from the 4th.
    static double anchor(int harmonic) { return std::clamp((harmonic - 2) / 2.0, 0.0, 1.0); }

    // The very top (air, sibilance riding on a vowel) stays centred too.
    static double topFade(double hz) { return std::clamp((kTopHz - hz) / (kTopHz - kTopFromHz), 0.0, 1.0); }

    static constexpr double kGolden = 0.6180339887498949;
    static constexpr double kTopFromHz = 6000.0, kTopHz = 9000.0;
};

} // namespace numinos::kite

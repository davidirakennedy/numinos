// Pitch — what note a voice is singing, and how sure that is.
//
// The YIN method (de Cheveigné & Kawahara, 2002), kept small. For every candidate period it asks:
// how different is the sound from itself, shifted by that much? A periodic sound (a sung vowel)
// matches itself almost perfectly one period later; breath, sibilance and noise never do. The best
// period gives the pitch, and how well it matched gives the confidence: near 1 on a vowel, near 0
// on an "s" or in a gap.
//
// It listens at a reduced rate (~11–12 kHz), which is plenty for a voice's pitch and makes it cheap.
// Feed every sample with push(); call analyse() whenever you want an estimate of the most recent
// stretch (about 40 ms). analyse() doesn't allocate.
#pragma once

#include "Biquad.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace numinos::dsp
{

class Pitch
{
  public:
    struct Estimate
    {
        double hz;         // the pitch (0 when there's nothing to go on)
        double confidence; // 0 .. 1: how periodic the sound is
    };

    void prepare(double sampleRate)
    {
        step = std::max(1, (int)std::floor(sampleRate / kListenRate));
        rate = sampleRate / step;
        smoothA = Biquad::lowPassFirstOrder(kBandLimitHz, sampleRate);
        smoothB = smoothA;
        window = (int)std::lround(kWindowSeconds * rate);
        minLag = std::max(2, (int)std::floor(rate / kHighestHz));
        maxLag = (int)std::ceil(rate / kLowestHz);
        history.assign(window + maxLag + 2, 0.0);
        difference.assign(maxLag + 2, 0.0);
        reset();
    }

    void reset()
    {
        smoothA.reset();
        smoothB.reset();
        std::fill(history.begin(), history.end(), 0.0);
        newest = 0;
        phase = 0;
    }

    void push(double x)
    {
        const double y = smoothB.process(smoothA.process(x)); // band-limit before listening at the lower rate
        if (++phase < step)
            return;
        phase = 0;
        newest = (newest + 1) % (int)history.size();
        history[newest] = y;
    }

    Estimate analyse()
    {
        // d(lag): how different the latest `window` samples are from themselves `lag` samples earlier.
        double energy = 0.0;
        for (int i = 0; i < window; ++i)
            energy += sample(i) * sample(i);
        if (energy < kSilence * window)
            return {0.0, 0.0};

        for (int lag = 1; lag <= maxLag + 1; ++lag)
        {
            double d = 0.0;
            for (int i = 0; i < window; ++i)
            {
                const double e = sample(i) - sample(i + lag);
                d += e * e;
            }
            difference[lag] = d;
        }

        // Normalise each by the average of the shorter lags, so "matches itself" reads near 0 and
        // "no better than chance" near 1, whatever the level (the "cumulative mean normalised
        // difference"). Then take the first clear dip, or failing that the deepest.
        double running = 0.0;
        for (int lag = 1; lag <= maxLag + 1; ++lag)
        {
            running += difference[lag];
            difference[lag] = running > 0.0 ? difference[lag] * lag / running : 1.0;
        }
        int best = -1;
        for (int lag = minLag; lag <= maxLag; ++lag)
            if (difference[lag] < kThreshold)
            {
                while (lag + 1 <= maxLag && difference[lag + 1] < difference[lag])
                    ++lag;
                best = lag;
                break;
            }
        if (best < 0)
            best = (int)(std::min_element(difference.begin() + minLag, difference.begin() + maxLag + 1) - difference.begin());

        // Between samples: the bottom of a parabola through the dip and its neighbours.
        const double a = difference[best - 1], b = difference[best], c = difference[best + 1];
        const double curve = a - 2.0 * b + c;
        const double offset = curve > 0.0 ? 0.5 * (a - c) / curve : 0.0;
        const double period = best + std::clamp(offset, -0.5, 0.5);

        const double confidence = std::clamp((kUnvoiced - b) / (kUnvoiced - kVoiced), 0.0, 1.0);
        return {rate / period, confidence};
    }

  private:
    // The i-th most recent sample at the listening rate (0 = newest).
    double sample(int i) const
    {
        const int size = (int)history.size();
        return history[(newest - i + size) % size];
    }

    static constexpr double kListenRate = 11025.0;  // listen at about this rate
    static constexpr double kBandLimitHz = 2500.0;  // and only to what's below this
    static constexpr double kWindowSeconds = 0.025; // compare 25 ms stretches
    static constexpr double kLowestHz = 70.0, kHighestHz = 1100.0; // the voice's range
    static constexpr double kThreshold = 0.15;      // a dip this deep is taken as the period
    static constexpr double kVoiced = 0.1, kUnvoiced = 0.35; // dip depths for full / no confidence
    static constexpr double kSilence = 1.0e-10;     // mean power below this: nothing to hear

    int step = 1, window = 1, minLag = 2, maxLag = 2;
    double rate = 11025.0;
    Biquad smoothA, smoothB;
    std::vector<double> history, difference;
    int newest = 0, phase = 0;
};

} // namespace numinos::dsp

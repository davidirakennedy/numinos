// KWeighting — the ITU-R BS.1770 loudness filter.
#pragma once

#include "Biquad.h"

#include <cmath>

namespace numinos::dsp
{

// K-weighting (ITU-R BS.1770), the filter loudness meters use to hear like an ear:
// a high shelf (about +4 dB above ~1.7 kHz) then a high-pass (~38 Hz).
// The constants are the standard's; these formulas give its filters at any sample rate.
class KWeighting
{
  public:
    void prepare(double sampleRate)
    {
        // Stage 1: high shelf. The ear is most sensitive in the upper mids.
        {
            const double gainDb = 3.999843853973347, q = 0.7071752369554196, fc = 1681.974450955533;
            const double k = std::tan(M_PI * fc / sampleRate);
            const double vh = std::pow(10.0, gainDb / 20.0);
            const double vb = std::pow(vh, 0.4996667741545416);
            const double a0 = 1.0 + k / q + k * k;
            shelf.b0 = (vh + vb * k / q + k * k) / a0;
            shelf.b1 = 2.0 * (k * k - vh) / a0;
            shelf.b2 = (vh - vb * k / q + k * k) / a0;
            shelf.a1 = 2.0 * (k * k - 1.0) / a0;
            shelf.a2 = (1.0 - k / q + k * k) / a0;
        }
        // Stage 2: high-pass. The ear barely hears rumble and plosive thumps as loudness.
        {
            const double q = 0.5003270373238773, fc = 38.13547087602444;
            const double k = std::tan(M_PI * fc / sampleRate);
            const double a0 = 1.0 + k / q + k * k;
            highPass.b0 = 1.0;
            highPass.b1 = -2.0;
            highPass.b2 = 1.0;
            highPass.a1 = 2.0 * (k * k - 1.0) / a0;
            highPass.a2 = (1.0 - k / q + k * k) / a0;
        }
        reset();
    }
    void reset()
    {
        shelf.reset();
        highPass.reset();
    }
    double process(double x) { return highPass.process(shelf.process(x)); }

  private:
    Biquad shelf, highPass;
};

} // namespace numinos::dsp

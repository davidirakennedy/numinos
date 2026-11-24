// Biquad — the standard second-order filter building block.
#pragma once

namespace numinos::dsp
{

// A second-order filter ("biquad"), in transposed direct form II.
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0; // coefficients (a0 is 1)
    double s1 = 0.0, s2 = 0.0;                               // state

    void reset() { s1 = s2 = 0.0; }
    double process(double x)
    {
        const double y = b0 * x + s1;
        s1 = b1 * x - a1 * y + s2;
        s2 = b2 * x - a2 * y;
        return y;
    }
};

} // namespace numinos::dsp

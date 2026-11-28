// Biquad — the standard second-order filter building block.
#pragma once

#include <cmath>

namespace numinos::dsp
{

// A second-order filter ("biquad"), in transposed direct form II.
struct Biquad
{
    // A band-pass with 0 dB at its centre (the "Audio EQ Cookbook" constant-peak form).
    // q = 1 is about 1.4 octaves wide.
    static Biquad bandPass(double centreHz, double q, double sampleRate)
    {
        const double w = 2.0 * M_PI * centreHz / sampleRate;
        const double alpha = std::sin(w) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        Biquad f;
        f.b0 = alpha / a0;
        f.b1 = 0.0;
        f.b2 = -alpha / a0;
        f.a1 = -2.0 * std::cos(w) / a0;
        f.a2 = (1.0 - alpha) / a0;
        return f;
    }

    // A gentle first-order low-pass (6 dB per octave), as a biquad with its second-order terms at 0.
    // Its complement (x minus this) is the matching high-pass, and the two are at right angles in
    // phase at every frequency (90 degrees apart), so their energies always add up to exactly the
    // input's. Steeper filters don't have that property.
    static Biquad lowPassFirstOrder(double cutoffHz, double sampleRate)
    {
        const double k = std::tan(M_PI * cutoffHz / sampleRate);
        Biquad f;
        f.b0 = f.b1 = k / (1.0 + k);
        f.a1 = (k - 1.0) / (k + 1.0);
        return f;
    }

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

// Spectrum — a sound in frequency, one in and two ears out (used by Kite).
//
// The voice is cut into overlapping frames (each ~43 ms, a new one every quarter of that), each
// frame is turned into its spectrum, every frequency can be given its own phase turn and its own
// gain, and the frames are turned back into sound and overlapped again. Each bin has two turns:
// one shared by both ears, and one split between them (the left ear gets it, the right gets the
// opposite); any pair of turns for the two ears is some mix of the two. And one gain, the same in
// both ears. With no turns and every gain at 1, what comes out is exactly what went in (only
// later).
//
// A phase turn changes *when* a frequency arrives, never how strong it is: whatever the turns,
// each ear on its own hears every frequency at exactly its original strength. Only the gains
// change strength, and they're the same in both ears.
//
// Use: push() every sample; when it returns true a frame is ready, so fill sharedTurns() and
// splitTurns() (radians, one per bin) and, if you like, gains() (they stay as you left them, 1 to
// begin with), and call render(); then pull() the sample for each ear. frame() is the frame's
// spectrum, to look at before rendering. Bin k is the frequency k * sampleRate / size().
#pragma once

#include "FFT.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace numinos::dsp
{

class Spectrum
{
  public:
    using Complex = FFT::Complex;

    void prepare(double sampleRate)
    {
        n = 1;
        while (n < kFrameSeconds * sampleRate)
            n *= 2;
        hop = n / 4;
        fft.prepare(n);

        // Square-root Hann on the way in and again on the way out: together a Hann, and Hanns a
        // quarter-frame apart add up to exactly 2 at every sample, so halving gives back the input.
        window.resize(n);
        for (int i = 0; i < n; ++i)
            window[i] = std::sqrt(0.5 - 0.5 * std::cos(2.0 * M_PI * i / n));

        input.assign(n, 0.0);
        outL.assign(n, 0.0);
        outR.assign(n, 0.0);
        bins.assign(n, Complex());
        shared.assign(n / 2 + 1, 0.0);
        split.assign(n / 2 + 1, 0.0);
        gain.assign(n / 2 + 1, 1.0);
        reset();
    }

    void reset()
    {
        std::fill(input.begin(), input.end(), 0.0);
        std::fill(outL.begin(), outL.end(), 0.0);
        std::fill(outR.begin(), outR.end(), 0.0);
        std::fill(shared.begin(), shared.end(), 0.0);
        std::fill(split.begin(), split.end(), 0.0);
        std::fill(gain.begin(), gain.end(), 1.0);
        pos = 0;
        sinceFrame = 0;
    }

    int size() const { return n; }
    int hopSamples() const { return hop; } // a new frame every this many samples
    int latencySamples() const { return n - 1; }

    // The newest frame covers the last size() samples pushed; its centre is half a frame back.
    int frameCentreAgo() const { return n / 2; }

    // The frame's spectrum (bins 0 .. size()/2), valid between push() returning true and render().
    const Complex* frame() const { return bins.data(); }

    // Each bin's turns: the same in both ears, and split between them (right = opposite of left).
    double* sharedTurns() { return shared.data(); }
    double* splitTurns() { return split.data(); }

    // Each bin's gain, the same in both ears (1 = as it is).
    double* gains() { return gain.data(); }

    bool push(double x)
    {
        input[pos] = x;
        pos = (pos + 1) % n;
        if (++sinceFrame < hop)
            return false;
        sinceFrame = 0;

        // The last n samples, oldest first, windowed, into the spectrum.
        for (int i = 0; i < n; ++i)
            bins[i] = Complex(input[(pos + i) % n] * window[i], 0.0);
        fft.forward(bins.data());
        return true;
    }

    void render()
    {
        // Each ear's spectrum is the voice's, turned. Both come back from one inverse transform:
        // put the left in the real part and the right in the imaginary part (each is the spectrum
        // of a real signal, so they don't mix). To keep each ear's output real, bin n-k must
        // mirror bin k.
        bins[0] = Complex(bins[0].real(), 0.0) * Complex(1.0, 1.0); // no turn at 0 Hz or at the top
        bins[n / 2] = Complex(bins[n / 2].real(), 0.0) * Complex(1.0, 1.0);
        for (int k = 1; k < n / 2; ++k)
        {
            const Complex left = bins[k] * std::polar(gain[k], shared[k] + split[k]);
            const Complex right = bins[k] * std::polar(gain[k], shared[k] - split[k]);
            const Complex i(0.0, 1.0);
            bins[k] = left + i * right;
            bins[n - k] = std::conj(left) + i * std::conj(right);
        }
        fft.inverse(bins.data());

        // Window again and overlap-add, each output sample into the slot of the input sample it
        // came from. A slot is played (and cleared) once every frame that covers it is done.
        for (int i = 0; i < n; ++i)
        {
            const int at = (pos + i) % n;
            outL[at] += 0.5 * window[i] * bins[i].real();
            outR[at] += 0.5 * window[i] * bins[i].imag();
        }
    }

    // The next sample for each ear: the oldest one still held, size() - 1 samples after it was
    // pushed. By now the last frame covering it has been rendered, so it's complete.
    void pull(double& left, double& right)
    {
        left = outL[pos];
        right = outR[pos];
        outL[pos] = outR[pos] = 0.0;
    }

  private:
    static constexpr double kFrameSeconds = 0.04; // the frame: the shortest power of two at least this long

    int n = 1, hop = 1;
    FFT fft;
    std::vector<double> window, input, outL, outR, shared, split, gain;
    std::vector<Complex> bins;
    int pos = 0, sinceFrame = 0;
};

} // namespace numinos::dsp

// FFT — the fast Fourier transform: a block of samples to its spectrum and back.
#pragma once

#include <cmath>
#include <complex>
#include <vector>

namespace numinos::dsp
{

// An in-place complex FFT for power-of-two sizes (iterative radix-2).
// forward() turns samples into a spectrum, inverse() turns it back, including the 1/n, so
// inverse(forward(x)) == x. Set the size in prepare(); transforms never allocate.
class FFT
{
  public:
    using Complex = std::complex<double>;

    void prepare(int newSize)
    {
        n = newSize;
        twiddles.resize(n / 2);
        for (int k = 0; k < n / 2; ++k)
            twiddles[k] = std::polar(1.0, -2.0 * M_PI * k / n);

        reversed.resize(n);
        int bits = 0;
        while ((1 << bits) < n)
            ++bits;
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                r |= ((i >> b) & 1) << (bits - 1 - b);
            reversed[i] = r;
        }
    }

    int size() const { return n; }

    void forward(Complex* x) const { transform(x, false); }
    void inverse(Complex* x) const
    {
        transform(x, true);
        for (int i = 0; i < n; ++i)
            x[i] /= (double)n;
    }

  private:
    void transform(Complex* x, bool inverse) const
    {
        for (int i = 0; i < n; ++i)
            if (i < reversed[i])
                std::swap(x[i], x[reversed[i]]);

        for (int length = 2; length <= n; length *= 2)
        {
            const int half = length / 2, stride = n / length;
            for (int start = 0; start < n; start += length)
                for (int k = 0; k < half; ++k)
                {
                    const Complex w = inverse ? std::conj(twiddles[k * stride]) : twiddles[k * stride];
                    const Complex a = x[start + k], b = x[start + k + half] * w;
                    x[start + k] = a + b;
                    x[start + k + half] = a - b;
                }
        }
    }

    int n = 0;
    std::vector<Complex> twiddles;
    std::vector<int> reversed;
};

} // namespace numinos::dsp

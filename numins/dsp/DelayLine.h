// DelayLine — a fixed-length history of past values.
#pragma once

#include <algorithm>
#include <vector>

namespace numinos::dsp
{

// A history of past values. read(0) is the newest, read(k) is k samples ago.
class DelayLine
{
  public:
    void prepare(int maxDelay) { buffer.assign(maxDelay + 1, 0.0); pos = 0; }
    void reset()
    {
        std::fill(buffer.begin(), buffer.end(), 0.0);
        pos = 0;
    }
    void write(double x)
    {
        pos = (pos + 1) % (int)buffer.size();
        buffer[pos] = x;
    }
    double read(int k) const
    {
        const int size = (int)buffer.size();
        return buffer[(pos - k + size) % size];
    }

  private:
    std::vector<double> buffer;
    int pos = 0;
};

} // namespace numinos::dsp

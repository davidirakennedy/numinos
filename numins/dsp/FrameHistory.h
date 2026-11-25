// FrameHistory — values indexed by an ever-increasing frame number.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace numinos::dsp
{

// Values indexed by absolute frame number, keeping only the most recent `capacity` frames.
class FrameHistory
{
  public:
    void prepare(int capacity) { values.assign(capacity, 0.0); }
    void reset() { std::fill(values.begin(), values.end(), 0.0); }
    double& operator[](int64_t frame) { return values[index(frame)]; }
    double operator[](int64_t frame) const { return values[index(frame)]; }

  private:
    size_t index(int64_t frame) const
    {
        const int64_t size = (int64_t)values.size();
        return (size_t)(((frame % size) + size) % size);
    }

  public:

  private:
    std::vector<double> values;
};

} // namespace numinos::dsp

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
    double& operator[](int64_t frame)
    {
        const int64_t size = (int64_t)values.size();
        return values[(size_t)(((frame % size) + size) % size)];
    }

  private:
    std::vector<double> values;
};

} // namespace numinos::dsp

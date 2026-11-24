// MovingAverage — the average of the last N inputs, cheap at any N.
#pragma once

#include "DelayLine.h"

namespace numinos::dsp
{

// Average of the last `length` inputs, using a running sum (cheap at any length).
// `length` may change between calls: the window grows or shrinks to match.
class MovingAverage
{
  public:
    void prepare(int maxLength)
    {
        history.prepare(maxLength);
        reset();
    }
    void reset()
    {
        history.reset();
        sum = 0.0;
        count = 0;
    }
    double process(double x, int length)
    {
        history.write(x);
        sum += x;
        ++count;
        while (count > length) // drop the oldest values
        {
            --count;
            sum -= history.read(count);
        }
        while (count < length) // pull older values back in
        {
            sum += history.read(count);
            ++count;
        }
        return sum / count;
    }

  private:
    DelayLine history;
    double sum = 0.0;
    int count = 0;
};

} // namespace numinos::dsp

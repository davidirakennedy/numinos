// Segment — Honey's unit of work: one stretch of voice between two seams.
#pragma once

#include <array>
#include <cstdint>

namespace numinos::honey
{

// Honey analyses audio in 5 ms "frames"; segment boundaries are frame numbers.
constexpr double kFrameSeconds = 0.005;

// One stretch of voice between two seams: roughly a syllable or a word.
struct Segment
{
    int64_t start = 0, end = 0; // frames, [start, end)
    int phrase = 0;             // which phrase (run of voice between gaps) it belongs to
    double loudnessDb = 0.0;    // its perceived loudness
    double gainDb = 0.0;        // the cut the solver chose (always <= 0)
    bool locked = false;        // once it starts playing, its gain never changes
};

// The segments Honey currently knows about, oldest first, in a fixed-size ring (no allocation).
class SegmentList
{
  public:
    static constexpr int kCapacity = 64;

    int size() const { return count; }
    Segment& operator[](int i) { return items[(first + i) % kCapacity]; }

    void push(const Segment& s)
    {
        if (count == kCapacity)
            popFront();
        items[(first + count) % kCapacity] = s;
        ++count;
    }
    void popFront()
    {
        first = (first + 1) % kCapacity;
        --count;
    }
    void erase(int i)
    {
        for (int k = i; k < count - 1; ++k)
            (*this)[k] = (*this)[k + 1];
        --count;
    }
    void clear() { first = count = 0; }

  private:
    std::array<Segment, kCapacity> items{};
    int first = 0, count = 0;
};

} // namespace numinos::honey

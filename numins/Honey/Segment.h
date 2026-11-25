// Segment — Honey's unit of work: one stretch of voice between two seams.
#pragma once

#include <array>
#include <cstdint>

namespace numinos::honey
{

// Honey analyses audio in 5 ms "frames"; segment boundaries are frame numbers.
// I may dabble with some different values here, or even expose this as an adjustable parameter.
// Honey is designed for use on the voice, but perhaps it could have extracurricular applications that
// might be better mediated with different "frame" sizes
constexpr double kFrameSeconds = 0.005;

// One stretch of voice between two seams: roughly a syllable or a word. At least, that was sort of the inspiration
// behind a Segment. It's not as though I've measured this, it's more of a feel right now that different syllables or
// stretches within a word or phrase aren't being treated drastically differently in a way that would be jarring or unstable
struct Segment
{
    int64_t start = 0, end = 0; // frames, [start, end)
    int phrase = 0;             // which phrase (run of voice between gaps) it belongs to
    double loudnessDb = 0.0;    // its perceived loudness
    double aboveFloorDb = 0.0;  // how far its loudness sits above the noise floor
    double gainDb = 0.0;        // the gain the solver chose (always <= 0; the baseline when untouched)
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

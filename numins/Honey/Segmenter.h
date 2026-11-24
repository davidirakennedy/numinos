// Segmenter — Honey's "seeing": loudness, the noise floor, and where the seams go.
//
// Feed it the K-weighted power of every sample. It gathers 5 ms frames, tracks two
// loudness contours and the noise floor, finds the valleys between syllables and words,
// and turns each stretch of voice into a rated Segment in the list you pass in.
#pragma once

#include "Segment.h"

#include "../dsp/FrameHistory.h"
#include "../dsp/MovingAverage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace numinos::honey
{

class Segmenter
{
  public:
    void prepare(double sampleRate)
    {
        frameLength = std::max(1, (int)std::lround(kFrameSeconds * sampleRate));
        fastAverage.prepare(kFastFrames);
        loudAverage.prepare(kLoudFrames);
        fastDb.prepare(kHistoryFrames);
        loudDb.prepare(kHistoryFrames);
        voiced.prepare(kHistoryFrames);
        reset();
    }

    void reset()
    {
        fastAverage.reset();
        loudAverage.reset();
        fastDb.reset();
        loudDb.reset();
        voiced.reset();

        frame = 0;
        framePowerSum = 0.0;
        frameSampleCount = 0;

        recentBlocksDb.fill(kNoFloorYetDb);
        noiseFloorDb = kNoFloorYetDb;
        blockIndex = 0;
        blockPowerSum = 0.0;
        blockFrameCount = 0;

        inSegment = false;
        segmentStart = 0;
        segmentStartsPhrase = false;
        phrase = 0;
        closedSegment = false;
    }

    int samplesPerFrame() const { return frameLength; }

    // Feed one sample's K-weighted power. Returns true if a segment closed, i.e. the
    // list gained a segment and the cuts should be solved again.
    bool process(double power, SegmentList& segments)
    {
        closedSegment = false;
        framePowerSum += power;
        if (++frameSampleCount == frameLength)
        {
            analyseFrame(framePowerSum / frameLength, segments);
            framePowerSum = 0.0;
            frameSampleCount = 0;
            ++frame;
        }
        return closedSegment;
    }

    // A segment can be at most this long. It's decided ~165 ms after it ends, and both must fit
    // inside Honey's lookahead budget (see kLookaheadSeconds). Longer notes are split into
    // pieces; the S-curve gain keeps the joins smooth.
    static constexpr int kMaxSegmentFrames = 60;    // 0.3 s

  private:
    // ---- analysis -----------------------------------------------------------------

    void analyseFrame(double power, SegmentList& segments)
    {
        // Two loudness contours. A moving average is centred half its length back,
        // so each value is stored at the frame it actually describes.
        const int64_t fastFrame = frame - kFastFrames / 2;
        fastDb[fastFrame] = 10.0 * std::log10(fastAverage.process(power, kFastFrames) + 1.0e-12);
        loudDb[frame - kLoudFrames / 2] = 10.0 * std::log10(loudAverage.process(power, kLoudFrames) + 1.0e-12);

        updateNoiseFloor(power);
        voiced[fastFrame] = fastDb[fastFrame] > noiseFloorDb + kVoiceMarginDb ? 1.0 : 0.0;

        // We can only judge a frame once we've seen kValleySearchFrames beyond it.
        const int64_t decide = fastFrame - kValleySearchFrames;
        if (decide > kValleySearchFrames)
            decideFrame(decide, segments);
    }

    // The quietest 50 ms block in the last 10 s, never below -70 dB. Digital silence is skipped.
    // This made need some work because if a vocal is trimmed in the beginning such that there is no
    // lead-in or silence the actual onset of the vocal would probably be treated as silence?
    void updateNoiseFloor(double power)
    {
        blockPowerSum += power;
        if (++blockFrameCount < kFloorBlockFrames)
            return;

        const double blockDb = 10.0 * std::log10(blockPowerSum / blockFrameCount + 1.0e-12);
        if (blockDb > kDigitalSilenceDb)
        {
            recentBlocksDb[blockIndex] = blockDb;
            blockIndex = (blockIndex + 1) % kFloorBlocks;
            const double quietest = *std::min_element(recentBlocksDb.begin(), recentBlocksDb.end());
            noiseFloorDb = std::max(quietest, kLowestFloorDb);
        }
        blockPowerSum = 0.0;
        blockFrameCount = 0;
    }

    // ---- segmentation -------------------------------------------------------------

    void decideFrame(int64_t f, SegmentList& segments)
    {
        const bool isVoice = voiced[f] > 0.5, wasVoice = voiced[f - 1] > 0.5;

        if (isVoice && !wasVoice) // a phrase begins
        {
            ++phrase;
            openSegment(f, true);
        }
        else if (!isVoice && wasVoice) // a phrase ends
        {
            if (inSegment)
                closeSegment(f, true, segments);
            inSegment = false;
        }
        else if (isVoice && inSegment)
        {
            const int64_t length = f - segmentStart;
            if (length >= kMaxSegmentFrames || (length >= kMinSegmentFrames && isValley(f)))
            {
                closeSegment(f, false, segments);
                openSegment(f, false);
            }
        }
    }

    // A valley: the lowest point nearby, at least kValleyDepthDb below the peaks on both sides.
    // This could probably use some fine-tuning. And ultimately, it probably doesn't need to 1:1 conform
    // with syllables or words necessarily, we just need smooth behavior.
    bool isValley(int64_t f)
    {
        const double here = fastDb[f];
        for (int k = 1; k <= kValleyLocalFrames; ++k)
            if (fastDb[f - k] < here || fastDb[f + k] <= here)
                return false;

        double leftPeak = here, rightPeak = here;
        for (int k = 1; k <= kValleySearchFrames; ++k)
        {
            leftPeak = std::max(leftPeak, fastDb[f - k]);
            rightPeak = std::max(rightPeak, fastDb[f + k]);
        }
        return std::min(leftPeak, rightPeak) - here >= kValleyDepthDb;
    }

    void openSegment(int64_t f, bool startsPhrase)
    {
        inSegment = true;
        segmentStart = f;
        segmentStartsPhrase = startsPhrase;
    }

    void closeSegment(int64_t f, bool endsPhrase, SegmentList& segments)
    {
        // A blip of voice too short to be a word isn't worth a segment. And we don't really want to pull down the
        // weighted average of the percieved loudness, or else we could end up treating rather "normal" moments as "too loud", thus
        // pulling them down in a way that might feel more like pumping. 
        if (segmentStartsPhrase && endsPhrase && f - segmentStart < kMinSegmentFrames)
            return;

        Segment s;
        s.start = segmentStart;
        s.end = f;
        s.phrase = phrase;
        s.loudnessDb = rate(s.start, s.end);
        segments.push(s);

        foldValley(segments);
        closedSegment = true;
    }

    // OLD //
    // Here's where some fine tuning / better algorithm design could come into play. this is where the 'loudness'
    // of a segment is rated. right now it's just the average loudness of the segment. but right now this could mean that
    // segments with longer, softer tails could sort of 'dilute' the actual peak of the segment meaning that less gain attenuation
    // is actually applied because the whole segment's weighted loudness is being pulled down by the tail before attenuation is applied

    // NEW //
    // Perceived loudness of frames [a, b). The ear judges a word mostly by its loudest part,
    // not its average, so this is a "soft maximum" of the 100 ms loudness. kPeakEmphasis = 1
    // is the plain power average; higher values lean smoothly toward the loudest moment. 100 ms could be up
    // to question in the future
    double rate(int64_t a, int64_t b)
    {
        double sum = 0.0;
        for (int64_t f = a; f < b; ++f)
            sum += std::pow(10.0, kPeakEmphasis * loudDb[f] / 10.0);
        const double mean = sum / (double)std::max<int64_t>(1, b - a);
        return 10.0 * std::log10(mean) / kPeakEmphasis;
    }

    // Now that the newest segment is known, look at the one before it. If it's short and
    // well below its neighbours, it's the dip between two words, not a word: replace it
    // with a single seam at the bottom of the dip.
    // This step is very important in making sure that our whole weighted average, spying on
    // neighbours thing even works as intended.
    void foldValley(SegmentList& segments)
    {
        const int k = segments.size() - 2;
        if (k < 0)
            return;
        Segment& dip = segments[k];
        if (dip.locked || dip.end - dip.start >= kFoldFrames)
            return;

        const bool hasLeft = k > 0 && segments[k - 1].phrase == dip.phrase && !segments[k - 1].locked;
        const bool hasRight = segments[k + 1].phrase == dip.phrase;
        if (!hasLeft && !hasRight)
            return;

        double louder = -1.0e9;
        if (hasLeft)
            louder = std::max(louder, segments[k - 1].loudnessDb);
        if (hasRight)
            louder = std::max(louder, segments[k + 1].loudnessDb);
        if (dip.loudnessDb > louder - kValleyDepthDb)
            return;

        int64_t bottom = dip.start;
        for (int64_t f = dip.start; f < dip.end; ++f)
            if (fastDb[f] < fastDb[bottom])
                bottom = f;

        if (hasLeft && hasRight)
        {
            segments[k - 1].end = bottom;
            segments[k + 1].start = bottom;
        }
        else if (hasLeft)
            segments[k - 1].end = dip.end;
        else
            segments[k + 1].start = dip.start;

        if (hasLeft)
            segments[k - 1].loudnessDb = rate(segments[k - 1].start, segments[k - 1].end);
        if (hasRight)
            segments[k + 1].loudnessDb = rate(segments[k + 1].start, segments[k + 1].end);
        segments.erase(k);
    }

    // ---- constants ----------------------------------------------------------------

    static constexpr int kFastFrames = 6;           // 30 ms contour: finds the valleys
    static constexpr int kLoudFrames = 20;          // 100 ms of integration. At smaller values, quick events (like transients)
                                                    // would register more strongly but this isn't exactly how the 'ear' hears loudness
    static constexpr int kHistoryFrames = 1024;     // ~5 s of analysis remembered

    static constexpr int kValleyLocalFrames = 8;    // a valley is the lowest point within 40 ms...

    static constexpr int kValleySearchFrames = 30;  // ...and dips below peaks within 150 ms each side...

    // How deep does a dip have to be in order to become a 'seam'? If this value is too low, individual moments or syllables within
    // words could be treated as different events (each then independently liable to be cut)
    static constexpr double kValleyDepthDb = 4.0;

    // This defines the shortest that a segment can be, 80 ms right now
    static constexpr int kMinSegmentFrames = 16;
    static constexpr int kFoldFrames = 30;          // dips shorter than 150 ms can fold into a seam

    static constexpr double kDigitalSilenceDb = -90.0;
    static constexpr double kLowestFloorDb = -70.0;
    static constexpr double kNoFloorYetDb = 0.0;     // before any blocks, treat nothing as voice
    static constexpr int kFloorBlockFrames = 10;     // 50 ms blocks...
    static constexpr int kFloorBlocks = 200;         // ...over the last 10 s
    static constexpr double kVoiceMarginDb = 12.0;   // voice is this far above the noise floor

    static constexpr double kPeakEmphasis = 6.0;   // 1 = average loudness .. higher = toward the peak

    // ---- state --------------------------------------------------------------------

    int frameLength = 1;
    dsp::MovingAverage fastAverage, loudAverage;
    dsp::FrameHistory fastDb, loudDb, voiced;

    int64_t frame = 0;
    double framePowerSum = 0.0;
    int frameSampleCount = 0;

    std::array<double, kFloorBlocks> recentBlocksDb{};
    double noiseFloorDb = kNoFloorYetDb, blockPowerSum = 0.0;
    int blockIndex = 0, blockFrameCount = 0;

    bool inSegment = false, segmentStartsPhrase = false;
    int64_t segmentStart = 0;
    int phrase = 0;
    bool closedSegment = false;
};

} // namespace numinos::honey

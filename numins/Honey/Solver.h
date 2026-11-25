// Solver — Honey's "judge and decide": how much to cut each segment.
#pragma once

#include "Segment.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace numinos::honey
{

class Solver
{
  public:

    // The most neighbours on each side that can count. Their weights fade with distance
    // (contextSeconds), so this is a ceiling on context, not how much is actually used.
    // I did dabble with some different values here. This may end up getting exposed as a param.
    static constexpr int kMaxNeighbours = 6;

    // Choose a gain for every segment that isn't playing yet. Every word sits at `baselineDb`
    // (a small attenuation, <= 0) unless it stands out from its neighbours:
    //   * a word ABOVE what its neighbours predict is cut further below the baseline;
    //   * a word BELOW what its neighbours predict is attenuated less: it rises from the
    //     baseline toward 0 dB, never beyond. So the most a word can be lifted is the baseline's
    //     depth, and since gaps sit at the baseline too, noise and breaths are never raised.
    // It minimises
    //   sum (gain - baseline)^2                          stay faithful to the performance
    //   + viscosity * sum upwardSurprise^2               words that jump out
    //   + viscosity * liftShare * sum downwardSurprise^2  words that get buried
    //   + viscosity * smoothness * sum glue(gain - previous gain)  neighbours in a phrase move together
    // where surprise is measured against a reference: what the neighbours predict, moved toward
    // the level of the whole line as `gather` rises (Honey's push range). Gathering makes Honey
    // pull the vocal together, not just smooth out what stands out.
    //
    // The smoothness term is what keeps all of that from reshaping the envelope. Without it, a quiet
    // syllable between two loud ones is lifted while they're cut, and the gain swings several dB
    // up and back down inside a single phrase: that's pumping, even with perfect S-curves. With it,
    // Honey prefers to move whole stretches of a phrase together, so it levels lines, not syllables.
    //
    // glue(d) is a square for small differences, leaning toward a straight line for large ones
    // (a "pseudo-Huber" curve; smoothKneeDb is where it turns). Small gain differences between
    // neighbours are the see-saw that reads as pumping, so they're resisted as strongly as ever.
    // But a big difference is what fixing a real surprise takes, and its cost grows only in
    // proportion, so the glue can no longer talk the solver out of cutting a word that jumps out.
    // Turning Viscosity up then really removes more of the surprise. The curve has no corner:
    // the pull between neighbours rises smoothly and levels off at about smoothKneeDb's worth.
    struct Settings
    {
        double viscosityWeight; // lambda: how strongly surprises are removed
        double contextSeconds;  // how far Honey looks: neighbours fade with distance at this rate
        double baselineDb;      // the density baseline every word and gap sits at (<= 0)
        double liftShare;       // how strongly buried words are lifted, relative to cutting
        double gather;          // 0..1: reference moves from the neighbours' prediction toward the line's level
        double smoothness;      // how strongly neighbouring segments of a phrase are kept at similar gains
        double smoothKneeDb;    // gain differences beyond about this are no longer resisted harder and harder
    };

    static void solve(SegmentList& segments, const Settings& settings)
    {
        const double baselineDb = settings.baselineDb;
        const double contextSeconds = settings.contextSeconds;
        const double gather = settings.gather;
        int first = 0;
    
        while (first < segments.size() && segments[first].locked)
            ++first;
        if (first == segments.size())
            return;
        first = std::max(0, first - kMaxNeighbours); // locked neighbours still count as context
        const int n = segments.size() - first;

        // Prediction: up to kMaxNeighbours each side, trusted less if far away or short.
        // weight[i][o + K] is how much the segment at offset o (-K..+K) counts in predicting segment i.
        constexpr int K = kMaxNeighbours;
        std::array<std::array<double, 2 * K + 1>, SegmentList::kCapacity> weight{};
        std::array<double, SegmentList::kCapacity> level{}, cut{}, penalty{}, surprise{}, grad{};
        std::array<double, SegmentList::kCapacity> liftPrediction{}, liftable{}, lineLevel{};
        for (int i = 0; i < n; ++i)
        {
            Segment& s = segments[first + i];
            level[i] = s.loudnessDb;
            cut[i] = s.gainDb;

            double total = 0.0;
            for (int o = -K; o <= K; ++o)
            {
                const int j = i + o;
                if (o == 0 || j < 0 || j >= n)
                    continue;
                Segment& nb = segments[first + j];
                const double distance = std::abs(centreSeconds(s) - centreSeconds(nb));
                const double length = (nb.end - nb.start) * kFrameSeconds;
                // A neighbour far quieter than the word (noise or a breath that slipped past the
                // noise floor) fades out of its prediction, so it can't drag the prediction down.
                const double quieter = s.loudnessDb - nb.loudnessDb;
                const double comparable = std::clamp(1.0 - (quieter - kQuietNeighbourDb) / kQuietFadeDb, 0.0, 1.0);
                weight[i][o + K] = std::exp(-distance / contextSeconds) * std::min(1.0, length / kWordSeconds) * comparable;
                total += weight[i][o + K];
            }
            for (double& w : weight[i])
                w = total > 0.0 ? w / total : 0.0;
            penalty[i] = std::min(1.0, total); // with no close neighbours there's nothing to surprise

            // The level of the line around this word: every word of its phrase in view counts equally,
            // however far, and words of other phrases count less (a new line shouldn't be judged by the
            // last one). Still ignoring much quieter words and trusting short ones less. Fixed: it's a
            // target, not something the solver can move by cutting other words.
            double lineSum = 0.0, lineWeight = 0.0;
            for (int j = 0; j < n; ++j)
            {
                if (j == i)
                    continue;
                Segment& other = segments[first + j];
                const double quieter = s.loudnessDb - other.loudnessDb;
                const double comparable = std::clamp(1.0 - (quieter - kQuietNeighbourDb) / kQuietFadeDb, 0.0, 1.0);
                const double samePhrase = other.phrase == s.phrase ? 1.0 : kOtherPhraseWeight;
                const double w = samePhrase * comparable * std::min(1.0, (other.end - other.start) * kFrameSeconds / kWordSeconds);
                lineSum += w * other.loudnessDb;
                lineWeight += w;
            }
            lineLevel[i] = lineWeight > 0.0 ? lineSum / lineWeight : s.loudnessDb;

            // Only words well clear of the noise floor can be lifted: never breaths or tails. And a short
            // fragment (often a word's tail fading into a gap) is lifted less: raising it and then
            // letting it fall away is exactly the swell we don't want.
            const double wordLength = std::min(1.0, (s.end - s.start) * kFrameSeconds / kWordSeconds);
            liftable[i] = std::clamp((s.aboveFloorDb - kLiftFloorMarginDb) / kLiftRampDb, 0.0, 1.0) * wordLength;
        }


        // Projected gradient descent: step downhill, then clamp every gain between the deepest
        // cut and the ceiling. The ceiling sits just above the baseline (it's negative, so 0.85 of
        // it is slightly higher): a buried word can rise only a little above "normal", ~0.5 dB at max.
        const double ceilingDb = 0.85 * baselineDb;
        const double lambda = settings.viscosityWeight;
        const double mu = lambda * settings.smoothness;
        const double step = 0.4 / (1.0 + 4.0 * lambda + 4.0 * mu); // smaller as the problem gets stiffer
        for (int iteration = 0; iteration < kSolverIterations; ++iteration)
        {
            for (int i = 0; i < n; ++i)
            {
                double predicted = 0.0;
                for (int o = -K; o <= K; ++o)
                    if (weight[i][o + K] > 0.0)
                        predicted += weight[i][o + K] * (level[i + o] + cut[i + o]);
                const double reference = (1.0 - gather) * predicted + gather * (lineLevel[i] + baselineDb);
                surprise[i] = penalty[i] * std::max(0.0, level[i] + cut[i] - reference);

                // For lifting, the same prediction, but only as a reference: no gradient flows to
                // the neighbours from it, so the only way to fix a buried word is to raise it,
                // never to pull the words around it down to its level. It uses the neighbours'
                // levels after their cuts, so a loud word that's being pulled in doesn't make the
                // words beside it look buried.
                liftPrediction[i] = reference;
            }
            for (int k = 0; k < n; ++k)
            {
                // How far below its neighbours the word sits, measured from the baseline.
                const double sunk = penalty[k] * liftable[k] *
                                    std::max(0.0, liftPrediction[k] - (level[k] + cut[k]));
                grad[k] = 2.0 * (cut[k] - baselineDb) + 2.0 * lambda * surprise[k] - 2.0 * lambda * settings.liftShare * sunk;
            }
            for (int k = 1; k < n; ++k) // neighbours in a phrase pull each other's gains together
                if (segments[first + k].phrase == segments[first + k - 1].phrase)
                {
                    const double pull = glueSlope(cut[k] - cut[k - 1], settings.smoothKneeDb);
                    grad[k] += 2.0 * mu * pull;
                    grad[k - 1] -= 2.0 * mu * pull;
                }
            for (int i = 0; i < n; ++i) // each neighbour j of i shares in i's surprise
                for (int o = -K; o <= K; ++o)
                    if (weight[i][o + K] > 0.0)
                        grad[i + o] -= 2.0 * lambda * surprise[i] * weight[i][o + K] * (1.0 - gather);
            for (int k = 0; k < n; ++k)
                if (!segments[first + k].locked)
                    cut[k] = std::clamp(cut[k] - step * grad[k], baselineDb - kMaxCutDb, ceilingDb);
        }

        for (int k = 0; k < n; ++k)
            if (!segments[first + k].locked)
                segments[first + k].gainDb = cut[k];
    }

  private:
    static double centreSeconds(const Segment& s) { return 0.5 * (s.start + s.end) * kFrameSeconds; }

    // The slope of glue(d) (halved, to match the square's 2d): d itself while small, levelling
    // off smoothly toward +/- knee once the difference is well past it.
    static double glueSlope(double d, double knee)
    {
        const double r = d / std::max(knee, 1.0e-6); // a knee of 0 would divide by zero
        return d / std::sqrt(1.0 + r * r);
    }

    

    // neighbours shorter than this count less, basically a safeguard against transients overweighting the algorithm
    // in one direction or another when in reality they have much less impact on percieved loudness to our ears
    static constexpr double kWordSeconds = 0.3;

    // Neighbours more than this much quieter than a word start to count less in its prediction,
    // and no longer count at all this much further down. Musical contrast lives well inside 15 dB;
    // noise and breaths sit far below it.
    static constexpr double kQuietNeighbourDb = 15.0;
    static constexpr double kQuietFadeDb = 10.0;

    // How much words of other phrases count in a word's line level (its own phrase counts fully).
    static constexpr double kOtherPhraseWeight = 0.3;

    // A safety ceiling: no word is ever cut more than this below the baseline.
    static constexpr double kMaxCutDb = 12.0;

    // A word must be this far above the noise floor before it can be lifted at all, and is fully
    // liftable this much further up. Keeps breaths, mouth noise and tails where they are.
    static constexpr double kLiftFloorMarginDb = 18.0;
    static constexpr double kLiftRampDb = 6.0;
    // if you start increasing the viscosity weight ceilings, you'll need to boost this value as well
    // because the solver's step size shrings as the lambda grows so you'll need more steps to settle fully.
    // initially set to 500
    static constexpr int kSolverIterations = 1000;
};

} // namespace numinos::honey

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
    static constexpr int kMaxNeighbours = 6;

    // `viscosityWeight` is the balance between the two (lambda). `contextSeconds` is how far
    // Honey looks: neighbours fade with distance at this rate, so a wider context lets a loud
    // run of words stand out against the line it's in, not just against the words beside it.
    static void solve(SegmentList& segments, double viscosityWeight, double contextSeconds)
    {
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
                weight[i][o + K] = std::exp(-distance / contextSeconds) * std::min(1.0, length / kWordSeconds);
                total += weight[i][o + K];
            }
            for (double& w : weight[i])
                w = total > 0.0 ? w / total : 0.0;
            penalty[i] = std::min(1.0, total); // with no close neighbours there's nothing to surprise
        }

        // Projected gradient descent: step downhill, then clamp every cut to <= 0.
        const double lambda = viscosityWeight;
        const double step = 0.4 / (1.0 + 4.0 * lambda);
        for (int iteration = 0; iteration < kSolverIterations; ++iteration)
        {
            for (int i = 0; i < n; ++i)
            {
                double predicted = 0.0;
                for (int o = -K; o <= K; ++o)
                    if (weight[i][o + K] > 0.0)
                        predicted += weight[i][o + K] * (level[i + o] + cut[i + o]);
                surprise[i] = penalty[i] * std::max(0.0, level[i] + cut[i] - predicted);
            }
            for (int k = 0; k < n; ++k)
                grad[k] = 2.0 * cut[k] + 2.0 * lambda * surprise[k];
            for (int i = 0; i < n; ++i) // each neighbour j of i shares in i's surprise
                for (int o = -K; o <= K; ++o)
                    if (weight[i][o + K] > 0.0)
                        grad[i + o] -= 2.0 * lambda * surprise[i] * weight[i][o + K];
            for (int k = 0; k < n; ++k)
                if (!segments[first + k].locked)
                    cut[k] = std::min(0.0, cut[k] - step * grad[k]);
        }

        for (int k = 0; k < n; ++k)
            if (!segments[first + k].locked)
                segments[first + k].gainDb = cut[k];
    }

  private:
    static double centreSeconds(const Segment& s) { return 0.5 * (s.start + s.end) * kFrameSeconds; }

    

    // neighbours shorter than this count less, basically a safeguard against transients overweighting the algorithm
    // in one direction or another when in reality they have much less percieved weight on our ears
    static constexpr double kWordSeconds = 0.3;
    static constexpr int kSolverIterations = 500;
};

} // namespace numinos::honey

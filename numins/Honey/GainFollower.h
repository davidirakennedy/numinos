// GainFollower — Honey's gain as a body with inertia ("getting there").
#pragma once

#include <algorithm>
#include <cmath>

namespace numinos::honey
{

// The gain moves through two critically damped stages in series: the first smooths the
// target, the second follows the first. Together every move is an S-curve: it begins with
// almost no acceleration (too gently to notice), commits through the middle, and settles
// without overshoot. Cuts and releases share the shape; releases can be made slower.
// this is the Brennan Monorail inspired design that allows us to get away with a little more gain attentuation
// but all transparently in a way that doesn't feel like clamping or compression.
//
// Releases can also be *shaped* (kReleaseCurve): the gain barely recovers at first and saves
// most of its recovery for the end of the move, when a fading tail is quietest and a change in
// gain is hardest to hear. Cuts are never shaped, so their imperceptible on-ramp is kept.
class GainFollower
{
  public:
    // Start at rest at `startDb` (Honey starts at its baseline, so it doesn't glide there).
    void reset(double startDb = 0.0)
    {
        first = Stage{startDb, 0.0};
        second = Stage{startDb, 0.0};
    }

    // Move one sample toward `targetDb`. `inertia` (seconds) is the gain's "mass" when cutting,
    // `releaseInertia` when recovering. Returns the new gain in dB.
    double process(double targetDb, double inertia, double releaseInertia, double sampleRate)
    {
        const bool releasing = targetDb > second.gainDb;
        const double stageInertia = (releasing ? kReleaseRatio * releaseInertia : inertia) / 2.0;
        const double dt = 1.0 / sampleRate;
        if (targetDb > first.gainDb)
            first.release(targetDb, stageInertia, dt); // recovering: shaped
        else
            first.step(targetDb, stageInertia, dt);    // cutting or holding: plain
        second.step(first.gainDb, stageInertia, dt);
        return second.gainDb;
    }

  private:
    // One spring-and-damper stage, critically damped.
    struct Stage
    {
        double gainDb = 0.0, velocity = 0.0;

        // A shaped release. Rather than gliding the gain itself, it glides a "progress" from 0 to 1
        // with the same spring-and-damper, and maps progress to gain through progress^kReleaseCurve.
        // At 1 that's the plain glide; above 1 the gain lingers near where it started and saves most
        // of the recovery for the end. A new target restarts the move from wherever the gain is.
        double moveFrom = 0.0, moveTo = 0.0, progress = 1.0, progressVelocity = 0.0;

        void release(double targetDb, double inertia, double dt)
        {
            if (targetDb != moveTo || progress >= 1.0)
            {
                moveFrom = gainDb;
                moveTo = targetDb;
                progress = 0.0;
                progressVelocity = 0.0;
            }

            const double omega = 1.0 / inertia;
            const double accel = omega * omega * (1.0 - progress) - 2.0 * omega * progressVelocity;
            progressVelocity += accel * dt;
            progress = std::min(1.0, progress + progressVelocity * dt);

            const double shaped = std::pow(progress, kReleaseCurve);
            const double before = gainDb;
            gainDb = moveFrom + (moveTo - moveFrom) * shaped;
            velocity = (gainDb - before) / dt; // so a cut arriving mid-release carries on smoothly
        }

        void step(double targetDb, double inertia, double dt)
        {
            progress = 1.0; // any shaped release in progress is over
            const double omega = 1.0 / inertia;
            const double accel = omega * omega * (targetDb - gainDb) - 2.0 * omega * velocity;
            velocity += accel * dt;
            gainDb += velocity * dt;

            // Cut only: stop at 0 dB, and drop any upward momentum with it.
            if (gainDb > 0.0)
            {
                gainDb = 0.0;
                velocity = 0.0;
            }
        }
    };

    // Release speed relative to cuts. 1 = same speed. Slower releases push the lift into
    // the next word (which then swells), so prefer shaping over slowing.
    static constexpr double kReleaseRatio = 1.0;

    // The shape of a release. 1 = the plain S-curve. 2 = most of the recovery saved for the end
    // of the move. 3 = more so. Higher values also make a release *finish* later, so if tails start
    // to feel held too long, raise Honey's kAnticipationRatio to start releases a little earlier.
    static constexpr double kReleaseCurve = 3.0;

    Stage first, second;
};

} // namespace numinos::honey
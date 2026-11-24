// GainFollower — Honey's gain as a body with inertia ("getting there").
#pragma once

namespace numinos::honey
{

// The gain moves through two critically damped stages in series: the first smooths the
// target, the second follows the first. Together every move is an S-curve: it begins with
// almost no acceleration (too gently to notice), commits through the middle, and settles
// without overshoot. Cuts and releases share the shape; releases can be made slower.
class GainFollower
{
  public:
    void reset()
    {
        first = Stage{};
        second = Stage{};
    }

    // Move one sample toward `targetDb`. `inertia` (seconds) is the gain's "mass".
    // Returns the new gain in dB.
    double process(double targetDb, double inertia, double sampleRate)
    {
        const bool releasing = targetDb > second.gainDb;
        const double stageInertia = (releasing ? kReleaseRatio : 1.0) * inertia / 2.0;
        const double dt = 1.0 / sampleRate;
        first.step(targetDb, stageInertia, dt);
        second.step(first.gainDb, stageInertia, dt);
        return second.gainDb;
    }

  private:
    // One spring-and-damper stage, critically damped.
    struct Stage
    {
        double gainDb = 0.0, velocity = 0.0;

        void step(double targetDb, double inertia, double dt)
        {
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

    Stage first, second;
};

} // namespace numinos::honey
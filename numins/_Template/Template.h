// __NAME__ — one line about what this Numin does.
// Created by scripts/new-numin.sh. Edit freely.
#pragma once

#include "../Numin.h"

namespace numinos
{

class __NAME__ : public Numin
{
  public:
    enum Param { Gain, Mix, NumParams };

    const char* name() const override { return "__NAME__"; }
    const char* category() const override { return "Uncategorized"; }
    const char* blurb() const override { return "A new Numin."; }

    int numParams() const override { return NumParams; }
    ParamInfo paramInfo(int i) const override
    {
        static const ParamInfo info[NumParams] = {
            {"Gain", 0.5f},
            {"Mix", 1.0f},
        };
        return info[i];
    }

    std::string displayValue(int i, float v) const override
    {
        if (i == Gain)
            return fmt("%+.1f dB", gainDb(v));
        return Numin::displayValue(i, v);
    }

    void reset() override {}

    void process(const float* inL, const float* inR, float* outL, float* outR, int frames) override
    {
        const double gain = dbToGain(gainDb(params[Gain]));
        const double mix = params[Mix];

        for (int n = 0; n < frames; ++n)
        {
            const double l = inL[n], r = inR[n];
            outL[n] = (float)lerp(l, l * gain, mix);
            outR[n] = (float)lerp(r, r * gain, mix);
        }
    }

  private:
    static double gainDb(float v) { return (v - 0.5) * 24.0; }
};

} // namespace numinos

// Numin.h — the one interface every Numin implements.
//
// A Numin is a plain C++ class with no framework dependencies (no JUCE, no
// plugin SDK). That's deliberate: the DSP lives
// on its own, and thin "hosts" wrap it — the DAW plugin, the render CLI, the
// safety checker. You write DSP here and never touch plugin boilerplate.
//
// Conventions:
//   * Stereo in, stereo out. Always.
//   * Parameters are normalized floats in 0..1. Each Numin maps them to real
//     units itself (dB, Hz, ...) and says how to display them.
//   * At most kMaxParams parameters.
//   * process() may be called with any block size, including 1, and in/out
//     may point to the same memory (in-place). tools/check verifies both.

#pragma once

#include <cmath>
#include <cstdio>
#include <string>

namespace numinos
{

constexpr int kMaxParams = 10;

struct ParamInfo
{
    const char* name;
    float defaultValue; // normalized 0..1
    float mark = -1.0f; // optional: a tick on the slider at this value, e.g. where a musical range ends
};

class Numin
{
  public:
    virtual ~Numin() = default;

    // ---- identity --------------------------------------------------------
    virtual const char* name() const = 0;     // unique, stable: saved in sessions
    virtual const char* category() const = 0; // groups the plugin's menu
    virtual const char* blurb() const = 0;    // one line, shown in the UI

    // ---- parameters ------------------------------------------------------
    virtual int numParams() const = 0;
    virtual ParamInfo paramInfo(int index) const = 0;

    // Text for a parameter at a given normalized value. Must be a pure
    // function of (index, value): the UI calls it from another thread.
    virtual std::string displayValue(int index, float value) const
    {
        (void)index;
        return fmt("%.0f%%", value * 100.0f);
    }

    float getParam(int index) const { return params[index]; }
    void setParam(int index, float value)
    {
        params[index] = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    }
    void resetParamsToDefaults()
    {
        for (int i = 0; i < numParams(); ++i)
            params[i] = paramInfo(i).defaultValue;
    }

    // ---- lifecycle -------------------------------------------------------
    // Called before audio starts and whenever the sample rate changes.
    virtual void prepare(double newSampleRate)
    {
        sampleRate = newSampleRate;
        reset();
    }

    // Clear all internal state (filters, delay lines, smoothers).
    virtual void reset() {}

    // ---- meters (optional) -----------------------------------------------
    // A Numin can publish a few live values for the plugin to draw: a Trace scrolls across a
    // strip over time (several traces share one dB scale), a Line is drawn across it (a level, say)
    // and a Shade shades everything below a level. A Gain is the gain the Numin is applying right
    // now, in dB (0 = unity), drawn as a needle beside the strip. The plugin reads them on the
    // audio thread straight after each process() call, so no extra thread safety is needed here.
    // Most Numins have none.
    // Put the input Trace first: while the plugin is bypassed it draws every other Trace as
    // equal to the first and every Gain as 0 dB, so the meters show the dry sound you hear.
    static constexpr int kMaxMeters = 5;
    enum class MeterKind { Trace, Line, Shade, Gain };
    struct MeterInfo
    {
        const char* name;
        MeterKind kind;
    };
    virtual int numMeters() const { return 0; }
    virtual MeterInfo meterInfo(int index) const
    {
        (void)index;
        return {"", MeterKind::Trace};
    }
    virtual float meterValue(int index) const
    {
        (void)index;
        return 0.0f;
    }
    // The dB range the strip shows, bottom to top.
    virtual void meterRangeDb(float& low, float& high) const
    {
        low = -80.0f;
        high = 0.0f;
    }

    // How many samples late this Numin's output is (lookahead). Valid after prepare().
    // The plugin reports it so the DAW can line the track back up with the rest of the mix.
    virtual int latencySamples() const { return 0; }

    // The audio. Called on the real-time thread: no allocation, no locks,
    // no file I/O, no printing.
    virtual void process(const float* inL, const float* inR, float* outL, float* outR,
                         int frames) = 0;

  protected:
    double sampleRate = 44100.0;
    float params[kMaxParams] = {};

    // ---- small helpers for subclasses ------------------------------------
    template <typename... Args> static std::string fmt(const char* f, Args... args)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), f, args...);
        return buf;
    }
    static double dbToGain(double db) { return std::pow(10.0, db / 20.0); }
    static double lerp(double a, double b, double t) { return a + (b - a) * t; }
};

} // namespace numinos

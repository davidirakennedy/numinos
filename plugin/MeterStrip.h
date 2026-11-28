// MeterStrip — a Numin's meters (see Numin.h) as a block of resin with the last few seconds of
// sound glowing inside it. The first Trace (what goes in) is the gold, a smooth mass rising and
// falling; later Traces (what comes out) are pale lines over it, so the gap between them is
// what the Numin did. Lines are dashed across it with their name and value, everything below a
// Shade is clouded, and a Gain is the reading in the corner ("moved −4.2 dB").
//
// The scale is focused, like most audio meters: the range where the signal actually lives gets
// most of the height, and the extremes are compressed. Here that's three bands, bottom to top:
//   silence   (everything below the Shade, e.g. Honey's silence band)   ~22% of the height
//   signal    (from there up to just above the recent loudest moment)  ~73%
//   headroom  (the rest, up to the top of the range)                    ~5%
// The band edges follow the signal slowly, so the scale settles rather than jumps.
// Double-click to switch to a plain linear scale over the whole range, and back.
#pragma once

#include "NuminosLook.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

class MeterStrip : public juce::Component, private juce::Timer
{
  public:
    using Kind = numinos::Numin::MeterKind;

    // `valueOf(i)` returns meter i's latest value; it's polled on the UI thread.
    MeterStrip(const NuminosLook& lookToUse, std::function<float(int)> valueOfMeter)
        : look(lookToUse), valueOf(std::move(valueOfMeter))
    {
        startTimerHz(kRefreshHz);
    }

    // Which meters to draw, from the current Numin's description. Clears the history.
    void configure(const NuminosProcessor::NuminView& view)
    {
        names = view.meterNames;
        kinds = view.meterKinds;
        lowDb = view.meterLowDb;
        highDb = view.meterHighDb;
        history.assign((size_t)names.size(), std::vector<float>(kHistory, kEmpty));
        times.assign(kHistory, 0.0);
        level.assign((size_t)names.size(), kEmpty);
        written = 0;
        silenceEdge = lowDb + 0.35f * (highDb - lowDb); // a neutral starting focus
        signalTop = highDb - 0.1f * (highDb - lowDb);
        gainIndex = kinds.indexOf(Kind::Gain);
        gainPosition = gainVelocity = 0.0f;
    }

    bool hasMeters() const { return names.size() > 0; }

    // How hard the Numin is working right now, 0..1 (from its Gain): the editor lets the
    // resin's glow spill a little further onto the stone while it is.
    float activity() const { return gainIndex < 0 ? 0.0f : juce::jmin(1.0f, std::abs(dbFor(gainPosition)) / 10.0f); }

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        linear = !linear;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        using C = NuminosLook::Colours;
        const float light = look.light;
        const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;

        auto well = getLocalBounds().toFloat();
        NuminosLook::drawWell(g, well, kRadius + kInset);
        const auto block = well.reduced(kInset);
        juce::Path shape;
        shape.addRoundedRectangle(block, kRadius);
        g.saveState();
        g.reduceClipRegion(shape);

        // the resin
        g.setGradientFill(juce::ColourGradient(C::resinTop, block.getTopLeft(), C::resinBottom, block.getBottomLeft(), false));
        g.fillRect(block);

        // the first trace: the gold, breathing slowly (its glow swells and settles every few seconds)
        const float breath = 0.8f + 0.2f * (float)std::sin(now * juce::MathConstants<double>::twoPi / 3.4);
        int firstTrace = kinds.indexOf(Kind::Trace);
        if (firstTrace >= 0)
        {
            g.setGradientFill(juce::ColourGradient(C::gold.withAlpha(0.9f * breath * light), block.getTopLeft(),
                                                   C::amber.withAlpha(0.05f * light), block.getBottomLeft(), false));
            for (auto& run : traceRuns((size_t)firstTrace, block, now))
                g.fillPath(closedToFloor(run, block));
        }

        // shades: everything below the level is clouded (for Honey, what it treats as silence)
        for (int i = 0; i < names.size(); ++i)
            if (kinds[i] == Kind::Shade && level[(size_t)i] > lowDb)
            {
                const float y = yFor(juce::jmin(level[(size_t)i], highDb), block);
                g.setColour(juce::Colours::black.withAlpha(0.32f));
                g.fillRect(block.withTop(y));
                g.setColour(juce::Colours::white.withAlpha(0.06f));
                g.fillRect(block.getX(), y, block.getWidth(), 1.0f);
                g.setColour(juce::Colour(0xffeccb9d).withAlpha(0.7f));
                g.setFont(look.caps(11.0f, 0.16f));
                g.drawText(names[i], block.withTop(y).reduced(14.0f, 6.0f), juce::Justification::bottomLeft);
            }

        // later traces: pale lines over the gold
        for (int i = firstTrace + 1; firstTrace >= 0 && i < names.size(); ++i)
            if (kinds[i] == Kind::Trace)
            {
                g.setColour(juce::Colour(0xfffff0d6).withAlpha(0.75f * light));
                for (auto& run : traceRuns((size_t)i, block, now))
                    g.strokePath(smooth(run), juce::PathStrokeType(1.4f, juce::PathStrokeType::curved));
            }

        // lines: dashed across, with their name and value above the line at the right
        g.setFont(look.caps(11.0f, 0.16f));
        float waitingY = block.getY() + 8.0f;
        for (int i = 0; i < names.size(); ++i)
            if (kinds[i] == Kind::Line)
            {
                const auto label = names[i] + "  ";
                if (inRange(level[(size_t)i]))
                {
                    const float y = yFor(level[(size_t)i], block);
                    const float dashes[] = {2.0f, 5.0f};
                    g.setColour(juce::Colour(0xfffff0d6).withAlpha(0.5f));
                    g.drawDashedLine(juce::Line<float>(block.getX(), y, block.getRight(), y), dashes, 2, 1.0f);
                    g.setColour(juce::Colour(0xfff6e6cc));
                    g.drawText(label + dbText(level[(size_t)i], 0) + " dB",
                               juce::Rectangle<float>(block.getX(), y - 20.0f, block.getWidth() - 14.0f, 16.0f),
                               juce::Justification::centredRight);
                }
                else
                {
                    g.setColour(juce::Colour(0xfff6e6cc).withAlpha(0.7f));
                    g.drawText(label + "listening...", juce::Rectangle<float>(block.getX(), waitingY, block.getWidth() - 14.0f, 16.0f),
                               juce::Justification::centredRight);
                    waitingY += 16.0f;
                }
            }

        drawBubbles(g, block, now);

        // the gloss across the top of the block
        g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.13f), block.getTopLeft(),
                                               juce::Colours::transparentBlack,
                                               block.getTopLeft() + juce::Point<float>(block.getWidth() * 0.18f, block.getHeight() * 0.5f),
                                               false));
        g.fillRect(block);

        // the traces' names, top left (and the scale, if it's been switched to linear)
        auto captions = block.reduced(14.0f, 9.0f).removeFromTop(14.0f);
        const auto nameFont = look.caps(11.0f, 0.16f);
        g.setFont(nameFont);
        for (int i = 0, t = 0; i < names.size(); ++i)
            if (kinds[i] == Kind::Trace)
            {
                g.setColour(t++ == 0 ? juce::Colour(0xffeccb9d) : juce::Colour(0xfffff0d6));
                const float w = juce::GlyphArrangement::getStringWidth(nameFont, names[i]) + 14.0f;
                g.drawText(names[i], captions.removeFromLeft(w), juce::Justification::centredLeft);
            }
        if (linear)
        {
            g.setColour(juce::Colour(0xffeccb9d).withAlpha(0.6f));
            g.drawText("linear scale", captions, juce::Justification::centredLeft);
        }

        // the Gain, as a reading in the corner
        if (gainIndex >= 0)
        {
            g.setColour(juce::Colour(0xfffbeedb));
            g.setFont(look.italic(15.0f));
            g.drawText(names[gainIndex].toLowerCase() + " " + reading(), block.reduced(14.0f, 8.0f),
                       juce::Justification::bottomRight);
        }
        g.restoreState();

        g.setColour(C::honey.withAlpha(0.2f));
        g.drawRoundedRectangle(block.reduced(0.5f), kRadius, 1.0f);
    }

  private:
    void timerCallback() override
    {
        if (!hasMeters() || !isShowing())
            return;
        const auto head = (size_t)(written % kHistory);
        times[head] = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        const auto previous = (size_t)((written + kHistory - 1) % kHistory);
        for (int i = 0; i < names.size(); ++i)
        {
            const float v = valueOf(i);
            if (kinds[i] == Kind::Trace)
            {
                // Smoothed as it's written (a gentle one-pole), so each point is fixed once it's
                // drawn: the curve only ever slides left, it never changes shape.
                const float before = history[(size_t)i][previous];
                history[(size_t)i][head] = v <= kEmpty || before <= kEmpty ? v : before + (v - before) * kTraceSmoothing;
            }
            else
                level[(size_t)i] = v;
        }
        ++written;
        updateFocus();
        if (gainIndex >= 0)
            moveGain(valueOf(gainIndex));
        repaint();
    }

    // ---- the traces -------------------------------------------------------------------------

    // A trace's recent history as runs of points, oldest first (a run breaks where there's no data
    // yet). The strip is a window onto a drawing that's already finished and sliding past: each
    // point is placed by *when* it was measured, so the scroll is steady however unevenly the timer
    // fires, and the newest kLeadSeconds sit just past the right edge, out of view. That's where a
    // stretch of curve takes its final shape (each stretch depends on the point after it); by the
    // time it slides into view it's settled, and it leaves past the left edge the same way.
    std::vector<std::vector<juce::Point<float>>> traceRuns(size_t meter, juce::Rectangle<float> plot, double now) const
    {
        std::vector<std::vector<juce::Point<float>>> runs(1);
        const auto& h = history[meter];
        const float pixelsPerSecond = plot.getWidth() / (float)kSpanSeconds;
        const int count = (int)juce::jmin<juce::int64>(written, kHistory);
        for (int ago = count - 1; ago >= 0; --ago)
        {
            const auto slot = (size_t)((written - 1 - ago) % kHistory);
            const float x = plot.getRight() - (float)(now - times[slot] - kLeadSeconds) * pixelsPerSecond;
            if (x < plot.getX() - kMarginPixels)
                continue; // long gone past the left edge
            if (h[slot] <= kEmpty)
            {
                if (!runs.back().empty())
                    runs.emplace_back();
                continue;
            }
            runs.back().push_back({x, yFor(h[slot], plot)});
        }
        runs.erase(std::remove_if(runs.begin(), runs.end(), [](auto& r) { return r.size() < 2; }), runs.end());
        return runs;
    }

    // A soft curve through the points: straight to the first, then a quadratic through each point
    // ending halfway to the next, so the corners round off into hills.
    static juce::Path smooth(const std::vector<juce::Point<float>>& p)
    {
        juce::Path path;
        path.startNewSubPath(p[0]);
        for (size_t i = 1; i + 1 < p.size(); ++i)
            path.quadraticTo(p[i], (p[i] + p[i + 1]) * 0.5f);
        path.lineTo(p.back());
        return path;
    }

    static juce::Path closedToFloor(const std::vector<juce::Point<float>>& p, juce::Rectangle<float> plot)
    {
        auto path = smooth(p);
        path.lineTo(p.back().x, plot.getBottom());
        path.lineTo(p.front().x, plot.getBottom());
        path.closeSubPath();
        return path;
    }

    // Two small bubbles rising slowly through the resin, each on its own cycle.
    void drawBubbles(juce::Graphics& g, juce::Rectangle<float> block, double now) const
    {
        struct Bubble { float x, size; double period, offset; };
        static constexpr Bubble bubbles[] = {{0.22f, 5.0f, 5.0, 0.0}, {0.61f, 4.0f, 6.5, 1.5}};
        for (auto& b : bubbles)
        {
            const float t = (float)std::fmod(now + b.offset, b.period) / (float)b.period; // 0..1 through its rise
            const float alpha = t < 0.15f ? t / 0.15f * 0.7f : 0.7f * (1.0f - (t - 0.15f) / 0.85f);
            g.setColour(juce::Colour(0xffffecc8).withAlpha(alpha * look.light));
            const float cy = block.getBottom() - 6.0f - b.size * 0.5f - t * 120.0f;
            g.fillEllipse(juce::Rectangle<float>(b.size, b.size).withCentre({block.getX() + block.getWidth() * b.x, cy}));
        }
    }

    // ---- the scale --------------------------------------------------------------------------

    // Move the focused band toward where the signal is: its bottom at the silence shade (where
    // silence ends), its top just above the loudest moment of the last few seconds. Slowly, so the
    // scale settles instead of jumping with every syllable.
    void updateFocus()
    {
        float edge = lowDb + 0.35f * (highDb - lowDb);
        for (int i = 0; i < names.size(); ++i)
            if (kinds[i] == Kind::Shade && inRange(level[(size_t)i]))
                edge = level[(size_t)i];

        float loudest = kEmpty;
        for (int i = 0; i < names.size(); ++i)
            if (kinds[i] == Kind::Trace)
                for (float v : history[(size_t)i])
                    loudest = juce::jmax(loudest, v);
        const float top = loudest > kEmpty ? juce::jlimit(edge + kMinSignalSpanDb, highDb, loudest + kHeadroomDb)
                                           : highDb - 0.1f * (highDb - lowDb);

        silenceEdge += (edge - silenceEdge) * kFocusSmoothing;
        signalTop += (juce::jmax(top, silenceEdge + kMinSignalSpanDb) - signalTop) * kFocusSmoothing;
    }

    // Height (0 at the bottom, 1 at the top) for a level, on the focused or the linear scale.
    float heightFor(float db) const
    {
        db = juce::jlimit(lowDb, highDb, db);
        if (linear)
            return (db - lowDb) / (highDb - lowDb);
        const float edge = juce::jlimit(lowDb + 1.0f, highDb - 2.0f, silenceEdge);
        const float top = juce::jlimit(edge + 1.0f, highDb, signalTop);
        if (db < edge)
            return kSilenceShare * (db - lowDb) / (edge - lowDb);
        if (db < top)
            return kSilenceShare + kSignalShare * (db - edge) / (top - edge);
        return kSilenceShare + kSignalShare + (1.0f - kSilenceShare - kSignalShare) * (db - top) / juce::jmax(0.01f, highDb - top);
    }

    float yFor(float db, juce::Rectangle<float> plot) const { return plot.getBottom() - plot.getHeight() * heightFor(db); }

    // A level is drawn only inside the meter's range (Honey's floor is off the top until it has
    // heard enough to know where the noise is).
    bool inRange(float db) const { return db >= lowDb && db <= highDb; }

    // ---- the gain reading -------------------------------------------------------------------

    // The reading has a little mass, like a meter movement: a spring pulls it toward the gain and
    // a damper, a touch under critical, lets it settle softly. It moves on a scale stretched near
    // zero (-1..1), so small moves are as calm as big ones. A few small steps per refresh keep it
    // stable.
    void moveGain(float db)
    {
        const float target = positionFor(db);
        const float omega = juce::MathConstants<float>::twoPi * kNaturalHz;
        const float dt = 1.0f / (float)(kRefreshHz * kSubsteps);
        for (int i = 0; i < kSubsteps; ++i)
        {
            gainVelocity += (omega * omega * (target - gainPosition) - 2.0f * kDamping * omega * gainVelocity) * dt;
            gainPosition = juce::jlimit(-1.0f, 1.0f, gainPosition + gainVelocity * dt);
        }
    }

    static float positionFor(float db)
    {
        const float size = std::log1p(std::abs(db) / kStretchDb) / std::log1p(kRangeDb / kStretchDb);
        return std::copysign(juce::jmin(1.0f, size), db);
    }

    static float dbFor(float p)
    {
        return std::copysign(kStretchDb * std::expm1(std::abs(p) * std::log1p(kRangeDb / kStretchDb)), p);
    }

    juce::String reading() const
    {
        const float db = dbFor(gainPosition);
        if (std::abs(db) < 0.05f)
            return "0.0 dB";
        return (db > 0.0f ? "+" : "") + dbText(db, 1) + " dB";
    }

    // A level with a true minus sign, to a fixed number of decimal places.
    static juce::String dbText(float db, int decimals)
    {
        const auto digits = juce::String(std::abs(db), decimals > 0 ? decimals : 1);
        const auto text = decimals > 0 ? digits : juce::String(juce::roundToInt(std::abs(db)));
        return (db < 0.0f && text != "0" ? juce::String::fromUTF8("\xe2\x88\x92") : juce::String()) + text;
    }

    static constexpr int kRefreshHz = 60, kSubsteps = 4;
    static constexpr double kSpanSeconds = 5.0;        // how long a moment takes to cross the strip
    static constexpr double kLeadSeconds = 0.1;        // how long it's drawn, out of view, before it enters
    static constexpr float kMarginPixels = 12.0f;      // points kept past the left edge as they leave
    static constexpr int kHistory = (int)(kRefreshHz * (kSpanSeconds + kLeadSeconds)) + 30; // with room to spare
    static constexpr float kTraceSmoothing = 0.3f;     // per frame: rounds off the jitter, about 50 ms of lag
    static constexpr float kEmpty = -1000.0f;
    static constexpr float kInset = 5.0f, kRadius = 16.0f; // the block inside its well

    // the focused scale
    static constexpr float kSilenceShare = 0.22f;      // of the height, for everything below silence
    static constexpr float kSignalShare = 0.73f;       // for the signal; the rest is headroom
    static constexpr float kHeadroomDb = 3.0f;         // the signal band ends this far above the loudest moment
    static constexpr float kMinSignalSpanDb = 18.0f;   // and is never narrower than this
    static constexpr float kFocusSmoothing = 0.02f;    // per refresh: settles over about a second

    // the gain reading's movement
    static constexpr float kRangeDb = 12.0f;           // where the scale ends, each side
    static constexpr float kStretchDb = 2.0f;          // how much it's stretched near zero (smaller = more)
    static constexpr float kNaturalHz = 1.6f;          // about a quarter second to arrive
    static constexpr float kDamping = 0.72f;           // 1 = critically damped

    const NuminosLook& look;
    std::function<float(int)> valueOf;
    juce::StringArray names;
    juce::Array<Kind> kinds;
    float lowDb = -80.0f, highDb = 0.0f;               // the meter's full range
    float silenceEdge = -60.0f, signalTop = -10.0f;    // the focused band, following the signal
    bool linear = false;
    std::vector<std::vector<float>> history;  // per meter (used by traces only), a ring of kHistory
    std::vector<double> times;                // when each slot of the ring was measured, in seconds
    std::vector<float> level;                 // per meter (used by lines and shades)
    juce::int64 written = 0;                  // frames written so far; the next goes at written % kHistory
    int gainIndex = -1;                       // which meter is the Gain (-1: none)
    float gainPosition = 0.0f, gainVelocity = 0.0f;
};

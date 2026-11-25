// MeterStrip — a scrolling picture of a Numin's meters (see Numin.h) over the last few seconds:
// its Traces on one shared dB scale, its Lines across them, and everything below a Shade shaded.
// For Honey: the loudness going in and coming out (the gap between them is Honey's gain), the
// noise floor, and the band Honey treats as silence, so you can see where to set Noise floor.
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

#include <functional>
#include <vector>

class MeterStrip : public juce::Component, private juce::Timer
{
  public:
    using Kind = numinos::Numin::MeterKind;

    // `valueOf(i)` returns meter i's latest value; it's polled on the UI thread.
    MeterStrip(const NuminosLook& look, std::function<float(int)> valueOf)
        : look(look), valueOf(std::move(valueOf))
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
        history.assign((size_t)names.size(), std::vector<float>(kPoints, kEmpty));
        level.assign((size_t)names.size(), kEmpty);
        head = 0;
        silenceEdge = lowDb + 0.35f * (highDb - lowDb); // a neutral starting focus
        signalTop = highDb - 0.1f * (highDb - lowDb);
    }

    // Anything for the strip to draw (a Gain goes to the needle instead).
    bool hasMeters() const
    {
        for (auto kind : kinds)
            if (kind != Kind::Gain)
                return true;
        return false;
    }

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        linear = !linear;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        using C = NuminosLook::Colours;
        auto area = getLocalBounds().toFloat();

        // caption: each trace's name in its colour on the left; each line's name and value on the right
        auto caption = area.removeFromTop(kCaptionHeight);
        const auto captionFont = look.caps(13.0f, 0.08f);
        g.setFont(captionFont);
        auto left = caption;
        for (int i = 0, t = 0; i < names.size(); ++i)
            if (kinds[i] == Kind::Trace)
            {
                g.setColour(traceColour(t++));
                const float w = juce::GlyphArrangement::getStringWidth(captionFont, names[i]) + 16.0f;
                g.drawText(names[i], left.removeFromLeft(w), juce::Justification::centredLeft);
            }
        juce::String right;
        if (linear)
            right << "linear scale    ";
        for (int i = 0; i < names.size(); ++i)
            if (kinds[i] == Kind::Line)
                right << names[i] << "  " << (inRange(level[(size_t)i]) ? juce::String(level[(size_t)i], 0) + " dB" : "listening...");
        g.setColour(C::amber);
        g.drawText(right, caption, juce::Justification::centredRight);
        area.removeFromTop(kCaptionGap);

        // the strip: a sheet of slightly deeper paper with a hairline edge
        g.setColour(C::paperDeep.withAlpha(0.6f));
        g.fillRect(area);
        g.setColour(C::hairline);
        g.drawRect(area, 1.0f);
        const auto plot = area.reduced(1.0f);
        g.saveState();
        g.reduceClipRegion(plot.toNearestInt());

        // grid: every 5 dB where there's room for it, so the focused band gets fine lines and the
        // compressed ends only a few; each line labelled on the left
        g.setFont(look.serif(11.0f));
        float lastY = 1.0e9f;
        for (float db = std::ceil(lowDb / 5.0f) * 5.0f; db <= highDb; db += 5.0f)
        {
            const float y = yFor(db, plot);
            if (lastY - y < kMinGridPixels)
                continue;
            lastY = y;
            g.setColour(C::hairline.withAlpha(0.5f));
            g.fillRect(plot.getX(), y, plot.getWidth(), 1.0f);
            g.setColour(C::faint);
            g.drawText(juce::String((int)db), juce::Rectangle<float>(plot.getX() + 3.0f, y - 12.0f, 30.0f, 11.0f),
                       juce::Justification::left);
        }

        // shades: everything below the level is (for Honey) treated as silence
        for (int i = 0; i < names.size(); ++i)
            if (kinds[i] == Kind::Shade && level[(size_t)i] > lowDb)
            {
                g.setColour(C::faint.withAlpha(0.18f));
                g.fillRect(plot.withTop(yFor(juce::jmin(level[(size_t)i], highDb), plot)));
            }

        // traces, oldest on the left: the first is drawn lighter, underneath; later ones on top
        for (int i = 0, t = 0; i < names.size(); ++i)
            if (kinds[i] == Kind::Trace)
            {
                g.setColour(traceColour(t));
                g.strokePath(tracePath((size_t)i, plot), juce::PathStrokeType(t == 0 ? 1.2f : 1.6f, juce::PathStrokeType::curved));
                ++t;
            }

        // lines, on top of everything
        for (int i = 0; i < names.size(); ++i)
            if (kinds[i] == Kind::Line && inRange(level[(size_t)i]))
            {
                g.setColour(C::amber);
                g.fillRect(plot.getX(), yFor(level[(size_t)i], plot) - 0.5f, plot.getWidth(), 1.5f);
            }
        g.restoreState();
    }

  private:
    void timerCallback() override
    {
        if (!hasMeters() || !isShowing())
            return;
        for (int i = 0; i < names.size(); ++i)
        {
            const float v = valueOf(i);
            if (kinds[i] == Kind::Trace)
                history[(size_t)i][(size_t)head] = v;
            else
                level[(size_t)i] = v;
        }
        head = (head + 1) % kPoints;
        updateFocus();
        repaint();
    }

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

    juce::Path tracePath(size_t meter, juce::Rectangle<float> plot) const
    {
        juce::Path path;
        bool started = false;
        for (int k = 0; k < kPoints; ++k)
        {
            const float v = history[meter][(size_t)((head + k) % kPoints)];
            if (v <= kEmpty) // no data yet
            {
                started = false;
                continue;
            }
            const float x = plot.getX() + plot.getWidth() * (float)k / (float)(kPoints - 1);
            const float y = yFor(v, plot);
            if (started)
                path.lineTo(x, y);
            else
                path.startNewSubPath(x, y);
            started = true;
        }
        return path;
    }

    // The first trace (for Honey, what goes in) is a soft graphite; the next (what comes out) ink-blue.
    static juce::Colour traceColour(int t)
    {
        using C = NuminosLook::Colours;
        return t == 0 ? C::faint : C::indigo;
    }

    // A level is drawn only inside the meter's range (Honey's floor is off the top until it has
    // heard enough to know where the noise is).
    bool inRange(float db) const { return db >= lowDb && db <= highDb; }

    static constexpr int kRefreshHz = 30;
    static constexpr int kPoints = kRefreshHz * 5;     // five seconds of history
    static constexpr float kEmpty = -1000.0f;
    static constexpr float kCaptionHeight = 18.0f, kCaptionGap = 4.0f;
    static constexpr float kMinGridPixels = 13.0f;     // grid lines closer than this are skipped

    // the focused scale
    static constexpr float kSilenceShare = 0.22f;      // of the height, for everything below silence
    static constexpr float kSignalShare = 0.73f;       // for the signal; the rest is headroom
    static constexpr float kHeadroomDb = 3.0f;         // the signal band ends this far above the loudest moment
    static constexpr float kMinSignalSpanDb = 18.0f;   // and is never narrower than this
    static constexpr float kFocusSmoothing = 0.04f;    // per refresh: settles over about a second

    const NuminosLook& look;
    std::function<float(int)> valueOf;
    juce::StringArray names;
    juce::Array<Kind> kinds;
    float lowDb = -80.0f, highDb = 0.0f;               // the meter's full range
    float silenceEdge = -60.0f, signalTop = -10.0f;    // the focused band, following the signal
    bool linear = false;
    std::vector<std::vector<float>> history; // per meter (used by traces only)
    std::vector<float> level;                 // per meter (used by lines and shades)
    int head = 0;                             // where the next point goes; also the oldest point
};

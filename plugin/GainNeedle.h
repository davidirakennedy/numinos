// GainNeedle — a Numin's Gain meter (see Numin.h) as a needle with mass, after the gain-reduction
// meters on the old 670s and LA-2As: you watch it move, rather than read a number. For Honey it's
// centre-zero: cuts swing the needle left, lifts swing it right, and at rest it sits upright.
//
// Two things make it readable rather than twitchy:
//   * the scale is stretched near zero, where Honey spends most of its time, and compressed toward
//     the ends (1 dB is about a fifth of the way out, 12 dB is the end stop);
//   * the needle is a small spring-and-damper, slightly underdamped like a real movement: it
//     follows the gain with a little lag and the faintest settle, so its *motion* shows the shape
//     of each move. A cut landing on an onset and letting go reads differently from a quick release.
#pragma once

#include "NuminosLook.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <functional>

class GainNeedle : public juce::Component, private juce::Timer
{
  public:
    // `valueOf(i)` returns meter i's latest value; it's polled on the UI thread.
    GainNeedle(const NuminosLook& look, std::function<float(int)> valueOf)
        : look(look), valueOf(std::move(valueOf))
    {
        startTimerHz(kRefreshHz);
    }

    // Find the current Numin's Gain meter, if it has one, and put the needle back at rest.
    void configure(const NuminosProcessor::NuminView& view)
    {
        index = view.meterKinds.indexOf(numinos::Numin::MeterKind::Gain);
        position = velocity = 0.0f;
    }

    bool hasGain() const { return index >= 0; }

    void paint(juce::Graphics& g) override
    {
        using C = NuminosLook::Colours;
        auto area = getLocalBounds().toFloat();

        // caption, matching the strip beside it: the name on the left, the reading on the right
        auto caption = area.removeFromTop(kCaptionHeight);
        g.setFont(look.caps(13.0f, 0.08f));
        g.setColour(C::graphite);
        g.drawText("Gain", caption, juce::Justification::centredLeft);
        g.setColour(C::amber);
        g.drawText(reading(), caption, juce::Justification::centredRight);
        area.removeFromTop(kCaptionGap);

        // the face: the same sheet of deeper paper as the strip
        g.setColour(C::paperDeep.withAlpha(0.6f));
        g.fillRect(area);
        g.setColour(C::hairline);
        g.drawRect(area, 1.0f);

        // Geometry: the pivot sits just above the bottom edge; the scale is an arc around it,
        // as large as fits with room for the numbers above it.
        const juce::Point<float> pivot(area.getCentreX(), area.getBottom() - kPivotInset);
        const float sweep = juce::degreesToRadians(kSweepDegrees);
        const float radius = juce::jmin(pivot.y - area.getY() - kLabelRoom,
                                        (area.getWidth() * 0.5f - 12.0f) / std::sin(sweep));
        auto at = [&](float angle, float r) { return pivot.translated(r * std::sin(angle), -r * std::cos(angle)); };
        auto angleFor = [&](float db) { return sweep * positionFor(db); };

        // the scale: a hairline arc, with the cut side drawn in ink-blue (like the Out trace) and
        // the lift side in amber, just inside it
        juce::Path arc;
        arc.addCentredArc(pivot.x, pivot.y, radius, radius, 0.0f, -sweep, sweep, true);
        g.setColour(C::ink.withAlpha(0.35f));
        g.strokePath(arc, juce::PathStrokeType(1.0f));
        juce::Path cutSide, liftSide;
        cutSide.addCentredArc(pivot.x, pivot.y, radius - 3.0f, radius - 3.0f, 0.0f, -sweep, 0.0f, true);
        liftSide.addCentredArc(pivot.x, pivot.y, radius - 3.0f, radius - 3.0f, 0.0f, 0.0f, sweep, true);
        g.setColour(C::indigo.withAlpha(0.45f));
        g.strokePath(cutSide, juce::PathStrokeType(2.0f));
        g.setColour(C::amber.withAlpha(0.55f));
        g.strokePath(liftSide, juce::PathStrokeType(2.0f));

        // ticks: zero tall and in ink; the numbered ones on both sides; small ones in between
        g.setFont(look.serif(11.0f));
        auto tick = [&](float db, float length, juce::Colour colour) {
            const float a = angleFor(db);
            g.setColour(colour);
            g.drawLine(juce::Line<float>(at(a, radius), at(a, radius + length)), 1.0f);
        };
        tick(0.0f, 7.0f, C::ink.withAlpha(0.8f));
        for (float db : {1.0f, 3.0f, 6.0f, 12.0f})
            for (float side : {-1.0f, 1.0f})
            {
                tick(side * db, 4.5f, C::graphite);
                const auto label = at(angleFor(side * db), radius + 11.0f);
                g.setColour(C::faint);
                g.drawText(juce::String((int)db), juce::Rectangle<float>(22.0f, 11.0f).withCentre(label),
                           juce::Justification::centred);
            }
        for (float db : {0.5f, 2.0f, 4.5f, 9.0f})
            for (float side : {-1.0f, 1.0f})
                tick(side * db, 2.5f, C::faint);

        // which way is which, tucked into the bottom corners
        g.setFont(look.caps(10.0f, 0.12f));
        g.setColour(C::faint);
        const auto corners = area.reduced(7.0f, 4.0f).removeFromBottom(12.0f);
        g.drawText("Cut", corners, juce::Justification::bottomLeft);
        g.drawText("Lift", corners, juce::Justification::bottomRight);

        // the needle, with a faint shadow so it sits just above the face; the honey drop is its pivot
        const float a = sweep * position;
        const auto tip = at(a, radius + 4.0f);
        g.setColour(C::ink.withAlpha(0.08f));
        g.drawLine(juce::Line<float>(pivot.translated(1.5f, 1.0f), tip.translated(1.5f, 1.0f)), 1.6f);
        g.setColour(C::ink.withAlpha(0.85f));
        g.drawLine(juce::Line<float>(pivot, tip), 1.3f);
        g.setColour(C::amber);
        g.fillPath(NuminosLook::dropShape(juce::Rectangle<float>(7.0f, 10.0f).withCentre(pivot.translated(0.0f, -1.0f))));
    }

  private:
    void timerCallback() override
    {
        if (!hasGain() || !isShowing())
            return;

        // The needle's movement: a spring pulling it toward the gain, and a damper, a little under
        // critical (kDamping < 1) so it settles with the faintest give. Integrated in a few small
        // steps per refresh to stay stable. It moves in scale units (-1..1), so it behaves the same
        // all along the scale.
        const float target = positionFor(valueOf(index));
        const float omega = juce::MathConstants<float>::twoPi * kNaturalHz;
        const float dt = 1.0f / (float)(kRefreshHz * kSubsteps);
        for (int i = 0; i < kSubsteps; ++i)
        {
            velocity += (omega * omega * (target - position) - 2.0f * kDamping * omega * velocity) * dt;
            position = juce::jlimit(-1.0f, 1.0f, position + velocity * dt);
        }

        // Repaint only while something visible is changing.
        if (std::abs(position - painted) > 0.0005f)
        {
            painted = position;
            repaint();
        }
    }

    // Where a gain sits on the scale, -1 (the left end stop) .. 1 (the right): logarithmic in the
    // gain's size, so small moves near zero get room and the ends are compressed.
    static float positionFor(float db)
    {
        const float size = std::log1p(std::abs(db) / kStretchDb) / std::log1p(kRangeDb / kStretchDb);
        return std::copysign(juce::jmin(1.0f, size), db);
    }

    // The inverse: the gain the needle is showing.
    static float dbFor(float p)
    {
        const float size = kStretchDb * std::expm1(std::abs(p) * std::log1p(kRangeDb / kStretchDb));
        return std::copysign(size, p);
    }

    // The reading follows the needle, not the raw value, so the number and the needle agree.
    juce::String reading() const
    {
        const float db = dbFor(position);
        if (std::abs(db) < 0.05f)
            return "0.0 dB";
        return (db > 0.0f ? "+" : juce::String::fromUTF8("\xe2\x88\x92")) + juce::String(std::abs(db), 1) + " dB";
    }

    static constexpr int kRefreshHz = 60, kSubsteps = 4;
    static constexpr float kCaptionHeight = 18.0f, kCaptionGap = 4.0f; // match MeterStrip
    static constexpr float kRangeDb = 12.0f;     // the end stops, each side
    static constexpr float kStretchDb = 2.0f;    // how much the scale is stretched near zero (smaller = more)
    static constexpr float kSweepDegrees = 50.0f; // each side of upright
    static constexpr float kPivotInset = 9.0f, kLabelRoom = 17.0f;
    static constexpr float kNaturalHz = 1.6f;    // the movement's speed: about a quarter second to arrive
    static constexpr float kDamping = 0.72f;     // 1 = critically damped; a touch under gives a soft settle

    const NuminosLook& look;
    std::function<float(int)> valueOf;
    int index = -1;            // which of the Numin's meters is its Gain (-1: none)
    float position = 0.0f, velocity = 0.0f, painted = 0.0f;
};

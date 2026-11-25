# Changelog

What's changed in Numinos, newest first. New work goes under **Unreleased**; when a version is
released, that section gets the version number and date, and a fresh Unreleased starts above it.
Format loosely follows [Keep a Changelog](https://keepachangelog.com); versions follow
MAJOR.MINOR.PATCH (still 0.x while things are early).

## Unreleased

### Added
- **Honey:** a gain needle beside the meter strip, showing what Honey is doing right now.
  Centre-zero: cuts swing left, lifts swing right. The needle has a little mass, so its motion
  shows the shape of each move.
- **Honey:** a live meter strip: loudness in and out, the noise floor, and the band Honey treats
  as silence. Its scale focuses on where the signal is; double-click for a linear scale.
- **Honey:** Noise floor control (Auto, or set by hand to where the noise sits).
- **Honey:** Output trim, for level-matching against bypass.
- **Honey:** density: words sit at a small baseline attenuation, so buried words can be lifted
  while noise and breaths never are.
- **Honey:** an About page showing its notes.
- A mark on a slider where a range changes (used by Viscosity at 80%).

### Changed
- **Honey:** Viscosity now has two ranges. Up to the 80% mark it's the musical range (what 100%
  used to be). Beyond it Honey pushes: stronger, quicker cuts, wider context, more lift, finer
  segments, and it starts gathering the vocal toward the level of the line.
- **Honey:** levels whole stretches of a phrase together instead of pushing syllables up and down
  against each other, which removes a pumping feel at high Viscosity.
- **Honey:** releases stay unhurried in the push range; only cuts get quicker.
- **Honey:** short word tails are lifted less, so they don't swell and then drop away.
- **Honey:** a new line is judged mostly against itself, not the line before it.
- **Honey:** lookahead shortened to 0.8 s so Logic compensates its latency.
- Bypass is latency-aligned, so A/B comparisons line up.

### Fixed
- **Honey:** a Noise floor setting that treated noise as voice could pull everything down.

## 0.1.0 – 2026-11-24

- First version of the collection: the plugin shell, the Numinos look, and Honey.

# Changelog

What's changed in Numinos, newest first. New work goes under **Unreleased**; when a version is
released, that section gets the version number and date, and a fresh Unreleased starts above it.
Format loosely follows [Keep a Changelog](https://keepachangelog.com); versions follow
MAJOR.MINOR.PATCH (still 0.x while things are early).

## Unreleased

### Changed (experimental)
- **Honey:** a new ear. Loudness is heard in five bands, weighted by how sensitive hearing is in
  each and added the way loudness adds, instead of one K-weighted power sum. Sibilance and
  consonants no longer read loud, and each segment is judged by its vowel.
- **Honey:** the gain is planned as one smooth curve across the whole lookahead, re-planned every
  5 ms, instead of a follower chasing a staircase of targets. Moving costs a lot on a vowel and
  almost nothing in valleys and gaps, so cuts land in the valley before a word, releases in its
  tail, and across a silence the gain glides from one line's level to the next.
- **Honey:** the levels are decided knowing what they'll cost to perform. Two neighbours can sit at
  different levels freely across a gap or a deep valley, where the move is hidden, but the
  syllables of a word are kept together unless a surprise really calls for splitting them.
  Honey's sense of where a gain move is audible is now one shared definition, used both to
  decide and to move.
- **Honey:** quiet things follow, they don't lead. Breaths, short fragments between words and tails
  that became segments of their own no longer get a level of their own (which left them uncut
  while the words around them were cut, a relative lift); they take their neighbours' level, and
  don't count in anyone's prediction.
- **Honey:** trail-offs are kept present. As a phrase fades into a rest, the gain gives back a share
  of the fall, up to a gentle cap: the tail still fades all the way, just more slowly. It slows the
  fall, never reverses it, and stops near the noise floor.
- **Honey:** gaps continue, they don't reset. Across a rest the gain glides from one line's level to
  the next instead of returning to the baseline in between (which made a rise in the rest, then a
  swing down into the next line).

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
- **Honey:** in near-silence after something loud, a rounding error could make the Ear's loudness
  NaN for a frame, which snapped the gain up to its maximum, mid-word if it happened there. Fixed
  at the source, and the gain path now refuses any bad number rather than letting it reach the audio.
- **Honey:** a Noise floor setting that treated noise as voice could pull everything down.

## 0.1.0 – 2026-11-24

- First version of the collection: the plugin shell, the Numinos look, and Honey.

# Numinos

Open-source audio effects by [Numinos LLC](https://numinos.art). Each effect is
a **Numin**, and all of them ship together as one plugin, **Numinos** (AU and
VST3, macOS): install it once, then pick a Numin from the menu.

## Layout

```
numins/          the DSP: one folder per Numin, plain C++, no framework
  Numin.h          the interface every Numin implements
  Registry.cpp     the list of Numins (scripts/new-numin.sh edits it for you)
  dsp/             shared building blocks (delay lines, averages, filters)
  Honey/           a larger Numin, split into one header per stage
  _Template/       what new Numins start from
tools/           command-line tools (no JUCE, so they build in seconds)
  render.cpp       numin-render: WAV in -> Numin -> WAV out, with stats
  check.cpp        numin-check: automated tests for every Numin
plugin/          the JUCE wrapper that turns the Numins into a DAW plugin
scripts/         everyday commands (below)
samples/         your test audio (git-ignored)
third_party/     dr_wav.h (WAV reading/writing, public domain)
```

The DSP never includes JUCE. You write a Numin against `Numin.h`, and the
plugin, the renderer and the checker all wrap that same class. You'll rarely
need to open `plugin/`.

## Setup (once)

```sh
xcode-select --install            # compilers (full Xcode is useful for debugging, not required)
brew install cmake ninja fswatch  # fswatch is optional; it makes the watch loops instant
git clone https://github.com/davidirakennedy/numinos.git && cd numinos
scripts/check.sh                  # first run downloads JUCE into .deps/ (~1 min)
```

## Working on a Numin

```sh
scripts/new-numin.sh Shimmer      # creates numins/Shimmer/Shimmer.h and registers it
```

Then choose the feedback loop that fits the moment.

**1. Ear loop: terminal, a few seconds per change**

```sh
scripts/listen.sh Shimmer samples/vox.wav Gain=0.7 --watch
```

This renders the sample through your Numin, prints peak/RMS before and after
(and flags NaNs or output over 0 dBFS), then plays the result. Every time you
save, it rebuilds, re-renders and plays again. Useful extras:

| | |
|---|---|
| `--sweep Gain` | move one parameter from 0 to 1 across the file, so you hear its whole range in one pass |
| `--ab 2` | switch between dry and processed every 2 seconds |
| `--tail 3` | add 3 s of silence at the end, for reverbs and delays |
| `Gain=0.7` or `p1=0.7` | set parameters (normalized 0–1) |

Renders are 32-bit float WAVs in `renders/`, so you can drop them into any
editor or analyzer.

**2. Hands loop: the lab app, live knobs**

```sh
scripts/lab.sh
```

This builds and opens Numinos as a standalone Mac app. Drag a WAV onto it and
it loops through the current Numin; turn knobs while it plays, and press
**A/B: Bypass** to compare with the dry sound. When you save code it rebuilds
and relaunches, and it comes back with the same Numin, knob positions and
sample. `printf`/`DBG` output shows up in the terminal. Your audio interface
input also works (Options → Audio Settings).

**3. Safety net**

```sh
scripts/check.sh                  # every Numin, seconds
scripts/check.sh --plugin         # also builds the plugin, runs auval + pluginval
```

`numin-check` runs each Numin at 44.1/48/96 kHz with silence, impulses, noise
and loud sines, at default, minimum, maximum and random settings. It fails on
the bugs that otherwise only show up in a client's session: NaN or inf output,
output that changes with the host's buffer size, state that `reset()` doesn't
clear, and processing that breaks in-place. CI runs it on every push.

**4. The DAW: only when it's time**

Every build also installs the plugin to `~/Library/Audio/Plug-Ins`, so it's
already in Logic (Audio FX → Numinos) for the final check in a real session.
Restart Logic to pick up a new build.

## Writing a Numin

```cpp
class Shimmer : public Numin {
  public:
    enum Param { Gain, Mix, NumParams };
    const char* name() const override { return "Shimmer"; }      // saved in sessions: don't rename after release
    int numParams() const override { return NumParams; }
    ParamInfo paramInfo(int i) const override;                   // name + default (0..1)
    std::string displayValue(int i, float v) const override;     // "+3.0 dB"
    void reset() override;                                       // clear all state
    void process(const float* inL, const float* inR,
                 float* outL, float* outR, int frames) override; // the sound
};
```

A few rules, all checked by `numin-check`:

- **Parameters are 0–1.** Map them to real units inside the Numin (see the mapping functions at the bottom of `Honey/Honey.h`).
- **Smooth per sample, not per block**, or the output depends on the host's buffer size.
- **Read both inputs before writing outputs.** Hosts process in place.
- **No allocation, locks, file I/O or printing in `process()`.** It runs on the real-time audio thread.
- Stereo in, stereo out. On mono tracks the plugin handles mono for you.

### A note per Numin

A Numin's folder can have its own `README.md`. GitHub shows it when you open the
folder, and the plugin embeds it and shows it under **About**. Write it so it reads
well as plain text: in the plugin, `#` heading marks are dropped and everything
else is shown as written.

## Releasing

```sh
scripts/package.sh                # -> dist/Numinos-0.1.0.pkg (AU + VST3, Apple Silicon + Intel)
```

Without signing you get a working but unsigned installer: fine for testing,
but other people's Macs will warn them. For a clean install experience you
need an Apple Developer account (Numinos LLC can enroll as an organization).
Then set the three environment variables at the top of `scripts/package.sh`
to sign and notarize. Bump the version in the top-level `CMakeLists.txt`,
tag (`git tag v0.1.0 && git push --tags`), and attach the `.pkg` to a GitHub
release.

**Never change** `PLUGIN_MANUFACTURER_CODE`, `PLUGIN_CODE` (in
`plugin/CMakeLists.txt`) or a released Numin's `name()`. DAW sessions use
them to find the plugin and the selected Numin.

## Formats

| Format | Status |
|---|---|
| AU (Logic, GarageBand, Ableton on Mac) | built |
| VST3 (Ableton, Cubase, Reaper, Studio One, ...) | built |
| Standalone app | built (the lab) |
| CLAP | not yet; [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) adds it |
| AAX (Pro Tools) | not yet; needs Avid's SDK and signing via PACE/iLok, so it's a separate step |

## Licensing

Everything in this repository is MIT licensed (see `LICENSE`): the Numins, the
tools, the wrapper. The built plugin also links [JUCE](https://juce.com), which
is dual-licensed under AGPLv3 or JUCE's own licence (with a free Starter tier
below a revenue limit). Because this project is open source, distributing the
plugin binary under AGPLv3 terms is a natural fit. The Numins and tools don't
use JUCE at all, so others can reuse them freely under MIT. This is a summary,
not legal advice.

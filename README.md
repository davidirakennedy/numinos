# Numinos

Free, open-source audio plugins I make under [Numinos](https://numinos.art).

Each one is a **Numin**: a small effect that tries to do one thing well. They all
live inside a single plugin called Numinos, so you install it once and choose a
Numin from its menu. Mac only for now, as AU and VST3.

## The Numins

- **[Honey](numins/Honey)**: vocal dynamics, one knob. Allows you to transparently
control percieved variation in percieved loudness over time. *In the making.*

More to come.

## Installing

There's no proper installer yet. I'm setting up an Apple Developer account so I
can sign one; until then macOS would warn you about anything I put out. When
there's a release, it'll be on this repo's Releases page.

If you're comfortable with a terminal, you can build it yourself (below). The
build puts the plugin straight into `~/Library/Audio/Plug-Ins`, and in Logic it
shows up under Audio FX → Numinos.

Pro Tools (AAX) isn't supported yet. It needs Avid's SDK and its own signing,
so it'll come later.

## Building from source

You'll need Xcode's command-line tools and a couple of things from Homebrew:

```sh
xcode-select --install
brew install cmake ninja
git clone https://github.com/davidirakennedy/numinos.git && cd numinos
scripts/check.sh            # builds and tests every Numin (first run downloads JUCE, ~1 min)
scripts/lab.sh              # opens Numinos as a standalone app: drop a WAV on it and play
scripts/package.sh          # builds an (unsigned) installer into dist/
```

Each Numin is plain C++ in `numins/<Name>/`, written against the small interface
in `numins/Numin.h`, with no plugin framework involved. `plugin/` is the JUCE
wrapper that turns them into a DAW plugin. If you want to try writing one,
`scripts/new-numin.sh YourName` starts you from `numins/_Template`.

## License

The Numins and tools are MIT licensed (see `LICENSE`), so you're free to reuse
them. The built plugin also uses [JUCE](https://juce.com), which comes with its
own licence terms (AGPLv3 or JUCE's commercial licence).

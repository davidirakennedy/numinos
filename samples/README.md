# samples/

Put test audio here: your own vocals and guitar, drum loops, anything. The
tools read WAV (any bit depth, mono or stereo); the lab app also reads AIFF,
FLAC and MP3.

Everything in this folder except this README is git-ignored. Your own
recordings would be fine to publish, but sample-pack licenses (Splice, LANDR
and so on) cover using the sounds in music, not handing out the raw files, so
keeping them out of a public repo is the safe default.

A useful starter set, each 10–30 seconds:

- `vox.wav` — dry vocal, some quiet and some loud phrases
- `gtr.wav` — acoustic guitar with clear transients
- `drums.wav` — a drum loop (transients and low end)
- `mix.wav` — a full mix, for bus-style Numins
- `sine-sweep.wav` — for measuring (`sox -n -r 48000 sine-sweep.wav synth 10 sine 20-20000 vol 0.5`)

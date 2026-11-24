# Honey

A note from the dev.

Let's be honest: if your vocal mix isn't sounding good, your MIX isn't sounding good. How is a good (vocal) mix often described? Velvety, thick, gluey, liquid, syrupy...at least in my corner of the audio world. A lot of these terms have something in common: viscosity. that's where "Honey" gets its name. Now into the design:

Most of the time, when you say you want to control a vocal's dynamics you really mean you want to control its *percieved* dynamics. Which is closely related to the signal's level, but a different thing in its own right. But most of tools we have available as audio engineers (i.e compressors) are better equipped to deal with a signal's digital level, which will of course impact it's percieved loudness, but more as a convenient side-effect.

Quite interestingly, it's possible to get a vocal that sounds "over-compressed" and still not somehow "controlled" in the way we're after. This is partly due to the fact that compressors are reactionary by design. This means that they will more reliably re-shape the envelope of a sound, because they rely first on that sound crossing a pre-defined threshold. Now of course, these are settings that we can dial in, but at their core compressors don't just change the percieved loudness of a source, they also change *how* that source sounds. People often reach for this purposefully. The grit of an 1176 for instance. Or the slow attack of an LA-2A that lets initial transients through. Or both, in series. 

If you want to control a vocal's dynamics without piling on these side-effects, you're essentially left with gain-automation as your last hope. And at this point, you're limited by your own expertise in drawing appropriate automation curves, which can be harder than it sounds. I built honey to save myself from my own incompetence here.

## How it works

A compressor operates on a signal's actual level, in more of an absolute sense. Honey doesn't care about absolute level or thresholds. Instead, it tries to keep the vocal from ever *surprising* you dynamically. Interestingly, if you sing a line softly at first, and slowly get louder as you do, this sounds rather natural and gradual to our ears: no surprises. But a compressor deosn't really care about how jarring or smooth an increase in level is. It only knows that when you cross a certain threshold, it's time to compress. If you crank the input gain of a compressor, eventually *everything* gets compressed. 

Honey sort of solves this problem. It doesn't react so much to absolute levels or thresholds, but instead keeps track of how tightly the percieved loudness of neighboring slices can predict the percieved loudness of the next slice. (Don't worry: no ai, no machine learning, just good ol' fashioned maths.) When a slice of sound deviates from its predicted loudness, Honey attenuates it very smoothly. Why is it so smooth? Honey basically implements a lookahead and "pre-fires" (my fellow Siege players may appreciate the term) the attenuation so that most of it is in place before the peak arrives. This means no aggressive re-shaping of the envelope a-la compressors above.

For the nerds who reaaaally want to get into the code, this system is actually inspired by the Brennan Monorail, which used a sophisticated system of gyroscopes to 'detect' and respond to *very* subtle changes in the balance of the train car...correcting them before passengers would even register them happening. Honey basically tries to keep gain automation as transparent as possible. This same S-style curve is what allows it to ease up on the end of phrases without introducing that 'pumping' effect where a tail is brought up too fast (or a compressor stops compressing too quickly)

## Using it

There's one knob, Viscosity. Turning it up makes Honey more likely to treat a slice in time as 'too surprising' and attenuate it therefore. It will also be a little quicker to do so. Importantly, Honey is *always* processing, even at a viscosity of 0. The idea is that you can often hear a compressor "engage" or "disengage". Some like this sound, but it's a sound nonetheless. Similar to vari-mu compression, Honey aims to be "threshold-less" so there's ideally never a moment where you "hear" it start to do something. By the time you notice anything, it's been doing quite a lot for a while. That's the hope at least.

Honey only ever turns things down, so the vocal will come out a little quieter. Match the levels before you compare with bypass, or the louder one will always seem better. I'm experimenting with an auto-gain system right now, but I may just opt for an ouput trim knob.

Honey is a mixing tool. It listens about 0.8 seconds ahead, so it adds that much latency. Your DAW compensates for it automatically during playback, but it's not for monitoring while you record.

## The Goal

Honestly, best case scenario is you throw this on a vocal, early on the chain and don't really hear too much. But when you bypass it the dry signal suddenly sounds less held-togther. Honey is really trying to give you what feels like a better dry signal to work with in the rest of your chain. It's trying hard not to come across as an "effect" being applied to the vocal.

Anyways, hope you enjoy!
— David Ira Kennedy

P.S
I'm currently working on getting my Apple Developer license setup properly. That's what will allow me to provide a proper installer (.dmg, .pkg) to smooth things over on your end. I beliiiiieve that right now if you try to install this code Apple will give you some security warnings because the package is unsigned. 

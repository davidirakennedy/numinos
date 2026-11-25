# Honey

# A note from the dev.

Let's be honest: if your *vocal* mix isn't sounding good, your MIX isn't sounding good. Nailing that is as important as anything. This almost certainly means figuring out how to control a vocal's dynamics, so it can sit well in the mix. Honey is really born out two realities along this front: 

— a signal's level and its perceived loudness are two related, but meaningfully different things.
— perceived loudness and *relative* perceived loudness are two different, but meaningfully related things.

When we say we want to control a vocal's dynamics, most of the time we're talking about that second thing: relative perceived loudness. But most of the tools we have available to us as audio engineers along this front (namely gain automation and compressors)are geared towards treating a signal's *level*. Which again, is going to impact a signal's perceived loudness at various points, but more as a well intended side-effect. 

Compressors introduce two problems:

— compressors are *reactive* by design, meaning they wait for a signal to cross a threshold before engaging. this means that a compressor will more reliably reshape a signal's *envelope*, which changes *how* it sounds as much as how loud it sounds. Now, people often reach for this purposefully. The grit of an 1176. The slow attack of an LA-2A that lets initial transients through. Or both, in series. But nonetheless, it *is* a side effect.
— a compressor doesn't care how smooth or jarring a change in a signal's level is, it cares about *absolute* level (whether or not it crosses a given threshold). A slow crescendo that crosses the threshold is still going to get damped just as much, even though to our ears that increase in volume doesn't read as problematic. 

And quite interestingly, it's very possible to get a vocal that sounds "over-compressed" and still not somehow "controlled" in the way that we're after. This happens when a compressor (or series of them) end up changing the envelope of your signal more noticeably than they seem to be controlling the jarring changes in perceived loudness of your vocal.

And if you want to control a vocal's dynamics without piling on these side-effects, you're essentially left with gain-automation as your last hope. And at this point, you're limited by your own expertise in drawing appropriate automation curves, which can be harder than it sounds. I built Honey to save myself from my own incompetence here.

# Enter Honey

How is a good (vocal) mix often described? Velvety, thick, gluey, liquid, syrupy...at least in my corner of the audio world. A lot of these terms have something in common: viscosity. that's where "Honey" gets its name. Let's get into it, shall we?

Honey doesn't react so much to absolute levels or thresholds like a compressor would. Instead, it tries to keep the vocal from ever *surprising* you dynamically. It does this by keeping track of how tightly the percieved loudness of a given slice of sound is predicted by the percieved loudness of its neighbouring slices. (Don't worry: no ai, no machine learning, just good ol' fashioned maths.) When a slice of sound deviates from its predicted loudness, Honey attenuates it very smoothly. 

Very smoothly. At least, that's the goal.

How is that done? Honey basically implements a lookahead and "pre-fires" (my fellow Siege players may appreciate the term) the attenuation so that most of it is in place before the peak arrives. This means no aggressive re-shaping of the envelope a-la compressors above. It tries to begin applying attenuation during the *valleys* of the signal, i.e when you're leasy likely to notice. It actually borrows a little trick from a 1900's invention called the Brennan Monorail. 

This was effectively a train which was able to ride on 1 track (instead of two, and even around turns). Without getting lost down that rabbit hole, it effectively used a sophisticated system of gyroscopes to 'detect' and respond to *very* subtle changes in the balance of the train car...correcting them before passengers would even register them happening. Honey basically tries to keep gain automation as transparent as possible by adopting this approach. This S-style curve is what allows it to ease in and out of what might otherwise be considered compression in a way that doesn't feel like it.

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

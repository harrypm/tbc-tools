# VHS-decode Auto Audio Align

A project to automatically align (RF) HiFi and linear audio captures to a video RF capture.
For this to work all 3 streams need to be captured with a synchronous clock source, like [cxadc-clock-generator-audio-adc](https://gitlab.com/wolfre/cxadc-clock-generator-audio-adc)
The app is written in C#/.NET, targeting Framework 4.7.2 and will work on Windows without additional requirements.
On linux / mac the amazing [mono project](https://www.mono-project.com) can be used to run.
The release [releases][releases] section also provides [Linux AppImage][appimage] builds that have the mono runtime included.

## Example for linear audio alignment

```bash
local rate_hz=46875
local channels=3
local bits_per_sample=24

# run VhsDecodeAutoAudioAlign.exe without arguments to get help on all possible options

sox -D \
	20231008-143140-linear-audio-46875sps-3ch-24bit-le.wav \
	-t raw -b $bits_per_sample -c $channels -L -e unsigned-integer - \
| mono VhsDecodeAutoAudioAlign.exe stream-align \
	--sample-size-bytes $(( $bits_per_sample / 8 * $channels )) \
	--stream-sample-rate-hz $rate_hz \
	--json video.tbc.json \
| sox -D \
	-t raw -r $rate_hz -b $bits_per_sample -c $channels -L -e unsigned-integer - \
	linear-aligned.wav
```

## Why do I need this

The following section will use the VHS format as an example, but this does apply to other cases as well.
You should also have a rough understanding of what an Analog-to-digital converter (ADC) is and how it works.
For starters you may want to have a read through the [wikipedia][wiki-adc] and watch this nice [introduction on the topic from xiph.org][yt-adc-intro].

So lets say you have captured a VHS, and used the [clock gen][clock-gen] to sync up all ADCs.
It doesn't actually matter how you captured those stream, just that all of them are clocked from the same clock source, I.e. they are synchronised.
What you end up with are 3 files:
- An RF capture of the video signal (from a CX card)
- An RF capture of the audio HiFi signal (from a CX card)
- A regular (baseband) capture of the linear audio (from the clock gen included PCM1802 ADC)

By using the [clock gen][clock-gen] (or another method to sync), you solved the problem of synchronization on the sample level.
That means if your two RF streams were both captured at same rate (e.g. 40MSps), then each sample in the video RF file will have a corresponding sample in the audio RF file.
The regular audio (from the PCM1802) is captured at a slower rate of course, but the same logic still applies.
Just that multiple RF video samples will correspond to a single audio sample.
As a simplified example you could imagine the linear audio to be captured at 40kHz (instead of the usual 48kHz).
Then for every sample in the audio you have 1000 corresponding samples in the video RF file.
The following image shows a visual mapping example with a 40MSps video RF, 20MSps audio RF and 40kSps PCM1802 capture (again numbers were chosen to be easier to visualize).

![sample-mapping.png](sample-mapping.png)

In the example you can see the samples aligning with exact numbers: 
every 2 RF video samples we have 1 RF audio, and every 1000 RF video samples we have 1 PCM1802 audio sample (with 3 channels).
This is exactly what the [clock gen][clock-gen] is achieving by clocking everything from a single clock source.

Great so now that's out of the way what is still missing?
Well we solved the problem of capturing the raw data in sync, but that does not mean your decoded video output is so too!
This is going to get complicated enough and for easier numbers lets say our VHS was PAL at 50 fields per second.

We'll talk the example through with the linear (baseband) audio as this is easier to picture, but the same also goes for the RF HiFi audio.
And again for round numbers we'll use 40kHz as the sample rate.
Every field of PAL video takes up 20ms (1s / 50 fields), and during this time 800 audio samples are taken (40000/50) by the PCM1802 audio ADC.
But the video recorder that is playing the VHS, what clock is it actually using?
Yes its not our clock generator, but yet another source, internal to the machine.
This means even if all our sampling is nicely in sync with each other, its out-of-sync with the playback (speed) of the VHS player.
And so the 50 fields actually could be something like 50.0001 (or 49.9999) fields per second.

Ok so maybe the player is a bit faster, why does that matter?
Well when decoding the RF video capture with [VHS-decode][vhs-decode], this information will not end up in the decoded video.
Instead of writing 50.0001 fields-per-second as the playback speed into the finished mkv (or what ever format you choose), it just a flat 50 fps.
So when you take the captured audio and put it next to the 50fps video, they will become out-of-sync over time.
The video will be played slightly to slow compared to its original speed, and over time this small error adds up.

![playout-error-accumulation.png](playout-error-accumulation.png)

Right so you may think this is a software bug in the [VHS-decode][vhs-decode], and needs to be fixed.
But its really not that simple, and there's mainly two reasons why.
For one you can't just put 50.0001 as the playback speed into the decoded file.
Some file formats may not even support this, and even if they do, playback systems are not well suited to cope with these odd numbers.
It may seem like odd numbers are not a problem, with NTSC having 59.97 fields per second.
But playout chains (be it in software players or hardware display devices), are very much tuned to a few known cases.
Others may work of course, but for good support sticking to those well known field/frame rates is the best option.

However even if that would work, the playback speed of your VHS player is most likely not constant.

![the-bends.png](the-bends.png)

The above image shows the relative change of speed across roughly 27 min of an actual VHS tape, as error of the projection in milliseconds (see details in next section).
The graph is a bit jittery / noisy due to the way it was measured (see details on field timing source in next section).
You can nicely see the bend, and this is what remains after removing the constant factor from the clock speed (that "0.0001" from the "50.0001").
For this example its not clear where those variations come from.
But in general these changes can be due to some mechanical properties of player or tape or electronic components heating up / cooling down.

From this its clear that we need to do something more than just putting in the right playback speed, into the decoded file.

If you are curious, you can draw this yourself (even from stand alone captures with a single CX card or DomesDayDuplicator) by running something like:

```bash
mono VhsDecodeAutoAudioAlign.exe gen-drift-csv --csv-output my-tape.csv --json video.tbc.json
```

The graph shows the "projection error" over "video time" and was plotted with [binjr][binjr], 
which interprets video time as an offset to unix epoch, hence the graph starting at 1970 1 AM.
You can use other tools like Excel or LibreOffice as well, but they are not handling large time series like this very efficiently.

## How this project solves this

The idea is simple: if we would know at what point in time a video field starts (ends), we could stretch / compress the audio to fit.
But how would we know?

We actually have two options:
One is with the [clock gen][clock-gen] which does not only capture dual channel audio from the PCM1802, but also the "head switch" signal on a 3rd channel.
And the head switch on VHS (and other formats) happens to be exactly in sync with the video fields (because one sweep across the tape for one head is exactly one field).
As all streams (RF video, RF audio, PCM1802, head switch) are in sync with each other we now know when fields start and end.
The other option is from the *tbc.json* which will actually record the field start offset from the RF capture.
And again because all stream are in sync this also tells us when fields start and end.

The *tbc.json* is actually less precise as the field starts are rounded (down) to the next block size in [VHS-decode][vhs-decode] (that's why the diagram above is jittery).
But it turns out this still precise enough to make it work.
As of now only the *tbc.json* is implemented as a field time source in the app, as this is more accessible, even without wiring up / tapping the head switch signal in the VCR.

Right, so with the frame timings known, now we can just stretch the audio to fit right?
Well not quite ... there's mainly two problems: the measured field time might be (very) jittering and we could have dropped fields.

So lets talk about the jitter problem first.
The app solves this by not taking the field times exactly as they are, but puts an idealised projection in between.
Here's an example:

![linear-projection.png](linear-projection.png)

The dashed line is the linear projection that is actually used to compress / stretch the audio to the video field times.
This is also the reason why the very jittery field times from the *tbc.json* work just as well, as the much more precise head switch signal.
With this the maximum audio-to-video offset error is about the maximum jitter.
So with the field times rounded to about 1 ms blocks by [VHS-decode][vhs-decode], the audio is up to 1 ms out-of-sync with the video, which is negligible.

This linear projection however does not solve the "the bend".
As of now the observed speed variations seem to be low enough to not really matter.
In the example about they are about 0.5 ms (500 us), which is again negligible.
However this could be improved by choosing a higher order polynomial than a straight line, as the projection curve.

On to the second problem of dropped fields, which can happen for various reason (e.g. tape damage, bad master, etc).
The following diagram shows what we have to work with (again using 40kSps audio and 50 fields per second to get round values)

![dropout-alignment.png](dropout-alignment.png)

As of Dec. 2023 [VHS-decode][vhs-decode] will just skip fields it can't decode.
So in the resulting decoded video (e.g. mkv) the 3 damaged fields from the tape (red), are just missing.
No timings will be adjusted, so when playing the mkv, it will just jump from RF field 2 (mkv field 2) to RF field 6 (mkv field 3).
The audio recording on the other hand will contain samples for those missing 3 fields, which now also need to be skipped to keep audio and video in sync.
The example shows this for audio from the PCM1802, but the same applies for HiFi audio.
As of Dec. 2023 the hifi decoder from [VHS-decode][vhs-decode], will produce output even for RF captures missing an actual hifi signal.

From the above example it is clear that the audio that belongs to the damaged / missing fields, needs to be skipped.
The app will identify missing fields by looking at the deltas between fields.
If the time between two fields is larger than a normal field time, this is counted as a "gap".
All the field (times) from a *tbc.json* (or in the future maybe head switch input) are split into "sections".
Each "gap" ends a section, and the linear projection is calculated per section, and not across the entire capture.
This makes sure that the audio aligns to the video, regardless of any (tape) issues.

## Interpolation

All of the above will in theory align the input audio to a the video timings given in the *tbc.json* (or in the future maybe head switch input).
The missing part here is interpolation of (input) audio samples.
When trying to output some audio sample the linear projection will tell us where to look in the input samples (it projects desired output samples to corresponding input samples).
But as this projection is continues (its a polynomial after all), mapping output sample *10000* e.g. will result in needing input sample *9998.7*.
That means from a *timing* perspective the desired output time of sample *10000* is actually between input samples *9998* and *9999*.

As we don't actually have that value in our original audio recording, so we need some sort of interpolation to approximate what that value would have been.
Interpolation is a very complex topic, and requires a lot of work to get results with sufficient quality.
This app uses the simplest kind of interpolation, giving worst possible results: nearest neighbor interpolation.
So in the above example, where a value for *9998.7* is desired, nearest neighbor will just round down and take the value from sample *9998*.
And over time our nearest neighbor interpolation will eventually skip one input sample entirely (or duplicate one input sample).

But this is not as bad as it sounds, and here's why can we get away with this.
The difference in speed between our VCR (which we are trying to compensate) and the ideal 50 fields per second, is very small in real life.
This also means that skipping or duplicating input samples, actually happens rarely, and so basically does not cause audible degradation in the result.

If you want to improve the outcome, then a simple way to do this is oversampling.
When you oversample the input signal with a factor of 2 lets say, then the *rounding* that occurs (skipping/duplicating input samples) is half as bad.
You can use [SoX][sox-wiki] to oversample to some factor, then use the auto alignment app, then downsample the output again to the original sample rate.
This nicely puts the effort of implementing a proper interpolation onto [SoX][sox-wiki] (which does do a very good job here).

And on a related note: for HiFi RF audio, you could opt to align the RF capture to the video and not the decoded HiFi output.
This way you already benefit from a sort of oversampling that has been done while capturing the RF, and don't need to redo it afterwards.

## Changelog

See [CHANGELOG.md](CHANGELOG.md).

## Releases

See [releases][releases] for a ready to use zip and [Linux AppImage][appimage] builds.

# License

The source code is mostly under 3-Clause BSD, but may contain external sources with compatible licenses mixed in.
The associated documentation (e.g. this markdown) is under *Creative Commons Attribution-ShareAlike 4.0*.

[clock-gen]: https://gitlab.com/wolfre/cxadc-clock-generator-audio-adc
[wiki-adc]: https://en.wikipedia.org/wiki/Analog-to-digital_converter
[yt-adc-intro]: https://www.youtube.com/watch?v=cIQ9IXSUzuM
[vhs-decode]: https://github.com/oyvindln/vhs-decode
[binjr]: https://github.com/binjr/binjr
[sox-wiki]: https://en.wikipedia.org/wiki/SoX
[releases]: https://gitlab.com/wolfre/vhs-decode-auto-audio-align/-/releases
[appimage]: https://appimage.org/

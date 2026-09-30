# AI Music

A small C++20 host for continuously generated instrumental audio. Change its
controls in the terminal or save a command file while it plays. The audio
engine, controls, and recorder are shared by three sources:

- **synth**: live house/trance synthesis: kick, clap, hats, bass, arpeggio, and
  chords. Every audio block is synthesized while it plays. This is procedural
  music, not neural AI generation.

- **demo**: a procedural synth with pads, an arpeggio, and optional percussion.
  This is a useful development source; it is not AI.
- **magenta**: the actual Magenta RealTime 2 small model, running locally through
  Google's C++/MLX inference engine.

The current host targets Apple Silicon macOS. Codex can act as the composer/DJ
through this chat by editing a score while C++ performs it continuously.

**Current limitation:** local real-time AI tests miss audio deadlines on this
M1. Use offline mode for AI recordings. The procedural source supports live
playback; uninterrupted real-time AI is still a performance milestone to reach.

## Run your personal station

```sh
python3 scripts/station.py
# Open http://127.0.0.1:8799 and click Listen.
# For the progressive-trance program instead:
python3 scripts/station.py --program trance
# Or play directly to the Mac's speakers:
./build/ai-music --source synth --radio --program french-house --volume .4 --watch live/current.commands
```

The local browser player streams newly synthesized stereo PCM from C++ through
a bounded pipe and HTTP queue into an AudioWorklet. It uses Python's standard
library; there are no additional server packages or audio files to load. Pause
stops listening while the station keeps composing. The page shows the current
section and provides a mixer, breakdown/build/drop cues, hold, resume, and next
chapter. It binds only to `127.0.0.1`. Use `--port NUMBER` if needed. Ctrl-C in
pane 2 stops the local server and its audio engine. Browser buffering adds
roughly 180 ms plus device/network scheduling; live controls update the engine.
After restarting the server, refresh the page and click Listen again.

The automatic director creates successive 64-bar chapters: sixteen bars of a
theme, sixteen of development, an eight-bar breakdown, eight-bar build, and
sixteen-bar release. The browser station defaults to **French house at 124 BPM**:
three original themes with syncopated bass, accented electric-key seventh chords,
and sparse hooks with held notes. Bass turnarounds, chord accents, and hooks
change within each chapter. Harmony moves every two bars in this program.

`--program trance` selects four progressive-trance themes at **132 BPM**, with
four-bar lead phrases, eight-chord progressions, changing bass pitches/rhythms,
and pluck/wide/soft lead timbres. The native C++ CLI defaults to this program.
Drumless breakdowns remove kick-driven sidechain pumping. Neither director
changes tempo; manual tempo commands remain available. Each program continues
until stopped, with bounded memory. This is a procedural arranger written by Codex,
not a continuously running language model or neural waveform generator.
No separate local AI installation or API key is needed for this workflow.

Describe changes to Codex here; it can edit `live/current.commands` atomically.
The whole score is validated before it replaces future cues. The audio clock
and existing effect tails continue. `cancel` stops scheduled cues and the
automatic director, holding the current groove. `quit` stops playback.
A 24-hour uninterrupted run has not yet been validated. Internet/YouTube
streaming and per-listener stations are future work; the current priority is
this listener's local sound. The current programs have a
finite musical vocabulary and does not yet provide album-quality composition.

## Play and mix house/trance live

```sh
./build/ai-music --source synth --watch examples/live-house.commands
```

It starts immediately, plays through the default audio output, and continues
until `quit` or Ctrl-C. It does not play a recorded audio file. Edit and save
`examples/live-house.commands`, or type commands into the running program:

```text
style trance
tempo 136
mix bass 0.7
mix lead 0.5
mix kick 0
filter 800
delay 0.35
```

`mix` controls kick, clap, hats, bass, lead, and pad independently. Set a layer
to zero to remove it. `filter` is a low-pass cutoff in Hz; `filter 20000` opens
it fully. `delay` adds a stereo echo synced to the synth's tempo. In the live
synth, these effects process the hook and chords; drums and bass use a separate
dry bus so their attacks stay clear. The bass has its own resonant filter that
opens on each note, and the kick has a shorter, saturated body and a small
attack transient. Effects process the full mix for the demo and neural sources.
All processing is included if you pass `--record`.
The trance preset is `examples/live-trance.commands`; an original French-house
score is `examples/live-french-house.commands`. Changing styles and tempo
keeps the musical clock running. Chords move every four bars by default, and the arp and
drum fill patterns vary over the phrase. Master/layer levels and filter/echo
changes are smoothed to avoid abrupt jumps.

Scheduled scores support new melodies, bass rhythms, transposition, and ramps:

```text
quantize 4
at 0 melody 0 2 3 2 4 2 3 5 0 2 3 2 4 5 6 2
at 0 bassline 0 1 1 1 0 1 1 1 0 1 1 1 0 1 1 1
at 0 mix kick 0
ramp 0 8 filter 10000
ramp 0 8 mix lead .55
at 8 mix kick .9
at 8 mix bass .65
```

`quantize 4` starts at the next four-bar boundary; `at` offsets are zero-based
bars from that start. `ramp START DURATION COMMAND` fades from the value at its
start to the given target. Mix, master volume, tempo, cutoff, and delay can ramp;
cutoff sweeps logarithmically. Melody uses 16 sixteenth-note steps: chord-tone
degrees 0..7, `-` for rests, or `~` to hold the previous note without retriggering
or changing its pitch. A tie after a rest stays silent. Bassline uses 16 zeros/ones. `root 45` selects
A minor; MIDI roots 36..60 transpose the progression. Notes remain on the synth
clock; automation updates on audio chunks of at most 64 samples (1.34 ms), with
DSP smoothing. Rendering also splits at sequencer boundaries so a new phrase
is applied before its first note triggers. Timing follows the musical beat.

`harmony` sets eight minor-key chord degrees (0..6); `chord-bars 1..8` changes
the duration of each entry (default four bars).
`bassnotes` takes 16 steps: `-` rest, `0` root, `1` fifth, `2` octave, `3` third,
`4` seventh. `chord-voice keys` selects an electric-key sound with a diatonic
seventh; `chord-voice pad` restores the sustained triad sound. `chords` takes
16 accent levels: `0` rest, `1` soft, `2` normal, `3` accented. Use
`chords sustain` for continuous chords. The `pad` mixer channel controls both
chord voices. `voice`
selects `pluck`, `wide`, or `soft` with a short crossfade; `rhythm` selects
`steady`, `drive`, or `build`. These are discrete controls that can use `at`.
`next` cues a new theme, `cancel` holds the current groove, and `radio on`
resumes the automatic director.

Each new timed score replaces future cues and freezes previous ramps at their
current value. Plain terminal controls cancel automation for that parameter.
Plain watched presets replace the previous program; comment-only files leave
it running. Scores allow up to 256 controls. With `--radio`, the director returns
after a custom score finishes and holds for at least eight more bars.

## Try the lightweight demo

Requires Xcode command-line tools, CMake 3.27+, and a C++20 compiler.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
./build/ai-music --watch examples/live-demo.commands
```

In another editor, change `style ambient` to `style pulse`, turn `drums on`, or
change `tempo`. Save the file to apply the changes without restarting playback.
You can also type commands directly into the running program. `quit` or Ctrl-C
stops and finalizes the recording.

## Build and run the AI source

The first build downloads native dependencies (MLX, TensorFlow Lite,
SentencePiece, and their dependencies). The model download is about 1.42 GB.
Dependencies are pinned in `cmake/Magenta.cmake`; model downloads are checked
against the sizes and checksums in `scripts/download_models.py`.
Use MLX's prebuilt C++ library to shorten the build. Python is used to install
the package; the running application does not embed Python. Keep this environment
in place because the executable links its native MLX library.

```sh
python3 -m venv .deps/native-python
.deps/native-python/bin/python -m pip install 'mlx==0.31.1'
python3 scripts/download_models.py
cmake -S . -B build-ai -DAI_MUSIC_MAGENTA=ON -DCMAKE_BUILD_TYPE=Release \
  -DAI_MUSIC_MLX_ROOT="$(.deps/native-python/bin/python -m mlx --cmake-dir)"
cmake --build build-ai --target ai-music --parallel 2
./build-ai/ai-music --source magenta --offline --duration 30 \
  --record recordings/first-ai.wav
```

TensorFlow Lite and SentencePiece still compile from source on the first build.
Alternatively, omit `AI_MUSIC_MLX_ROOT` to compile MLX too. That path requires
Apple's Metal toolchain (`xcrun metal --version`); install it if needed with
`xcodebuild -downloadComponent MetalToolchain`. The source build uses `-O1` for
one slow MLX CPU fallback; the Metal inference path retains release optimization.
The prebuilt path follows [MLX's C++ guide](https://ml-explore.github.io/mlx/build/html/dev/mlx_in_cpp.html).

Run commands from the repository directory, or pass an absolute `--model-dir`
path. The first model start compiles GPU kernels and can take longer than later
starts. The small model is selected for the M1; the model's musical quality and
the host's ability to play continuously are separate things to evaluate.

Offline mode waits for every generated frame, so a slow model does not insert
silence into the recording. `--duration` is the resulting audio length; wall
time may be longer. It requires a recording path and a positive duration, never
opens the audio device, and still accepts terminal or watched-file controls.

Try the experimental live AI path, optionally recording a session:

```sh
./build-ai/ai-music --source magenta \
  --style "instrumental jazz trio, gentle piano, brushed drums, upright bass" \
  --record recordings/jazz.wav
```

Existing recordings are never overwritten. Recording streams to 48 kHz stereo
16-bit PCM WAV files and starts a new numbered file every hour to stay below
WAV's size limit. No full-session audio array is kept in memory.

## Live controls

```text
style instrumental ambient electronic, warm analog synths, soft percussion
volume 0.25
drums off
temperature 1.1
mute
unmute
status
quit
```

- `style` accepts free text for AI; the demo accepts `ambient` or `pulse`.
- The live synth accepts `house` or `trance`, plus `mix LAYER 0..1` controls.
- `volume` is linear gain between 0 and 1 and changes smoothly.
- `drums off` requests drumless generation; `on` permits percussion. AI controls
  influence the model and do not guarantee an exact arrangement.
- `temperature` controls AI sampling, between 0.1 and 2.
- `tempo` sets the synth/demo's exact BPM, between 30 and 240. Describe tempo in the AI
  prompt for now; this host does not expose exact AI tempo control.
- `mute` keeps generation and time moving while fading output to silence.

Watched files contain these musical controls, one per line, with optional `#`
comment lines. `status`, `help`, and `quit` are terminal-only. The entire saved
file is parsed and checked before any controls are changed. Invalid edits keep
the previous settings and print an error. Prompt encoding happens in the model's
worker thread while playback continues. The watched format is deliberately a
small command interface, not a general programming language.

## Check continuity without playing through speakers

```sh
./build-ai/ai-music --source magenta --no-audio --no-input \
  --duration 600 --record recordings/ten-minutes.wav
```

`--no-audio` still consumes at real-time speed. It checks that inference keeps up
without sending sound to the audio device; it is not an offline fast render.
`--no-input` permits unattended use. Omit `--duration` to run until stopped.

Status reports processed audio time, audio underruns, invalid samples, output
peak, generation time per 40 ms model frame, queued audio, prompt status
(`1` encoding, `2` ready, `3` failed), peak process memory, and recording drops.
Exit status `2` means the run encountered audio gaps or recording drops. A file
with gaps is retained for diagnosis. Larger buffers trade control latency for
resilience; this host uses 160 ms within the upstream runner's capacity.

## Architecture and next pieces

```text
terminal / watched commands ──> Source controls
                               │
AI worker ──> bounded buffer ──> audio callback ──> speakers
                                      │
                                      └──> bounded queue ──> WAV writer thread
```

The audio callback performs no file I/O, model inference, or dynamic allocation.
Parsing and score composition happen outside it. Fixed-size score messages cross
a bounded queue; numeric automation is applied on the audio thread without
allocation or locks. `Source` is the boundary for different sound
generators; `Engine` owns gain, sanitization, metrics, and recording. The Magenta
adapter uses the upstream `RealtimeRunner` for inference and audio buffering.
Checked-in dependency patches make the prompt worker joinable before model
destruction, fix the handoff of prompts arriving as that worker finishes, keep
left and right audio reads synchronized, and make status snapshots safe across
threads. The inference worker is marked as
user-initiated work for macOS scheduling. Patches are applied during configuration
against the pinned upstream revision. Offline mode uses a separate blocking read
on its render thread; the audio-device callback always uses nonblocking reads.

Useful next steps are richer instrument voices, listening-driven refinement,
longer thematic development, and a 24-hour endurance test. A future
listener service can control separate engine instances through the same scores.

## Validation

```sh
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
python3 tests/live_controls.py build/ai-music
python3 tests/web_station.py
# Optional: ten-minute AI check with two live style changes and an audio report.
python3 scripts/check_stream.py
# Gap-free offline recording and live-control check (not a real-time test).
python3 scripts/check_stream.py --offline --duration 30
python3 tests/ai_shutdown.py
```

The tests exercise concurrent queue ordering and wraparound, malformed controls,
output sanitization and mute smoothing, WAV segmentation and shutdown, and
changes to a watched file during uninterrupted audio. They also check both live
dance styles, independent layer muting, delay tails, and low-pass attenuation.
Routing checks verify that musical filter/echo changes preserve the kick and
bass while still affecting the hook.
Score tests cover bar quantization, ramp interpolation and replacement, manual
overrides, cancellation, pattern validation, phrase downbeats, tied-note sustain
and release, complete French-house/trance arrangements, and tempo cues without
resetting the audio clock.
The local-web integration test checks nonzero live PCM, remote mixer changes,
transactional rejection of bad scores, same-origin controls, and clean shutdown.
Run logs and experimental
recordings belong in the ignored `validation/` and `recordings/` directories.

On the development M1 (16 GB), the core tests, watched-file integration test,
and muted CoreAudio smoke test passed. A 30-second offline AI render with two
prompt changes also passed: 1,440,000 stereo frames, no underruns, invalid samples,
or recording drops. It took 128.91 seconds including startup. The local listening
sample is `recordings/preview.wav`; its report is
`validation/ai-offline-stereo/report.json`.

The real-time 30-second trial failed with 2,219 underrun callbacks. Increasing
buffering cannot sustain playback when average generation is slower than audio
consumption. The ten-minute uninterrupted AI milestone has **not** been met.
The live station uses the synth while neural inference remains an independent
performance experiment.

Upstream: [Magenta RealTime 2](https://github.com/magenta/magenta-realtime),
[model release](https://huggingface.co/google/magenta-realtime-2).
Upstream code and model assets retain their respective licenses.

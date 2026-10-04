# AI Music

[![CI](https://github.com/beejmaxx/ai-music/actions/workflows/ci.yml/badge.svg)](https://github.com/beejmaxx/ai-music/actions/workflows/ci.yml)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)

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

The current host targets macOS; the neural Metal backend requires Apple Silicon.
Control it through the terminal, a watched command file, or the local browser
player. The procedural station needs no model download or API key.

To learn the neural audio path, start with
[Lesson 1: from a description to AI-generated audio](lessons/01-ai-audio.txt).
It explains training, inference, and playback, then traces a short prompt
experiment through the existing C++ integration.

**Experimental neural playback:** the five-second buffer configuration passed
a two-minute native test on an M1, but the ten-minute attempt still encountered underruns
under changing system load. Use offline mode for uninterrupted AI recordings.
The browser station uses the procedural synth. See [validation status](docs/validation.md).

## Quick start

Requires macOS, Xcode command-line tools, CMake 3.27+, and Python 3.10+.

```sh
git clone https://github.com/beejmaxx/ai-music.git
cd ai-music
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
python3 scripts/station.py
```

Open <http://127.0.0.1:8799> and click **Listen**. This starts procedural music;
the optional [neural build](#build-and-run-the-ai-source) downloads its own model
assets. See [CONTRIBUTING.md](CONTRIBUTING.md) for tests and development.

Source code is licensed under [Apache 2.0](LICENSE). Model weights are downloaded
separately under their upstream terms; see [THIRD_PARTY.md](THIRD_PARTY.md).

## Run your personal station

```sh
python3 scripts/station.py
# Open http://127.0.0.1:8799 and click Listen.
# For the progressive-trance program instead:
python3 scripts/station.py --program trance
# A short, repeating Da Funk sound-design study at 111 BPM:
python3 scripts/station.py --program funk-study
# Or play directly to the Mac's speakers:
./build/ai-music --source synth --radio --program french-house --volume .4 --watch live/current.commands
```

The local browser player streams newly synthesized stereo PCM from C++ through
a bounded pipe and HTTP queue into an AudioWorklet. It uses Python's standard
library; there are no additional server packages or audio files to load. Pause
stops listening while the station keeps composing. The page shows the current
section and provides a mixer, breakdown/build/drop cues, hold, resume, and next
chapter. It binds only to `127.0.0.1`. Use `--port NUMBER` if needed. Ctrl-C in
the terminal stops the local server and its audio engine. Browser buffering adds
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
until stopped, with bounded memory. This arranger uses procedural instruments
and score rules; neural generation is available through the separate Magenta source.
No separate local AI installation or API key is needed for this workflow.

`--program funk-study` runs a sixteen-bar reference study at a fixed **111 BPM**.
It uses a four-bar riff with notes anticipated before the barline and tied
across it, a distorted pulse voice with a second tone a fourth above, and a
short lead dropout to expose the drum/bass groove. The riff's upper contour
was revised against the local reference; the score specifies the lower member
of each fourth. It is an
approximation for sound-design comparison, not a verified note-for-note cover.
The voice design draws on the band-pass, interval, and overdrive approach in
[Reverb Machine's reconstruction](https://reverbmachine.com/blog/daft-punk-homework-synth-sounds/)
and [Syntorial's patch study](https://www.syntorial.com/preset-recipe/daft-punk-da-funk-lead/).
The user's reference MP3s stay in the ignored `recordings/references/` directory;
they are not loaded or sampled by the live engine.

The revised kit combines a short kick, a snare body with filtered clap bursts,
and metallic hats with different accents for closed, open, and ghost hits.
The grit voice runs its oscillators, resonant filter, and overdrive at twice
the sample rate, filters the result before downsampling, and uses lighter
kick ducking so sustained notes remain audible. Its bass part sits an octave
higher than the first study, with a new turnaround and a revised mix.

For short sound-design comparisons, build the project and render a chapter
through the same synthesis, mixer, and effects path as live playback:

```sh
./build/music-render --program funk-study recordings/funk-audition.wav 8.64865
# Or render a score file; the optional last argument is master volume (0..1).
./build/music-render examples/live-french-house.commands recordings/house-audition.wav 12 .36
```

`music-render` produces an audition file without opening an audio device.
Program auditions cover at most one chapter; score files run for the requested
duration. Existing output files are protected from overwrite. Compare patches
at matched listening volume and audition the instruments separately. Audio
integrity tests check timing, bounds, and dropouts; they do not establish that a
patch sounds good. Live playback continues to synthesize every block.

Edit `live/current.commands` to change the score while it plays.
The whole score is validated before it replaces future cues. The audio clock
and existing effect tails continue. `cancel` stops scheduled cues and the
automatic director, holding the current groove. `quit` stops playback.
A 24-hour uninterrupted run has not yet been validated. Internet/YouTube
streaming and per-listener stations are future work; the current priority is
this listener's local sound. The current programs have a
finite musical vocabulary and does not yet provide album-quality composition.

## Develop individual instruments

### Bass

`music-sound` renders the standalone bass instrument without drums, a score or
the neural model. Start with a single note and change one patch control:

```sh
cmake --build build --target music-sound --parallel 2
./build/music-sound bass sounds/bass/round.voice recordings/bass-round.wav --note 41 --gate 1
afplay recordings/bass-round.wav
```

Patch files expose saw/pulse/sine oscillators, pulse width, a sine sub oscillator,
filter cutoff and resonance (Q), a decaying filter envelope, amplitude ADSR,
pre-filter drive, glide and output level. Times are milliseconds; `filter-amount`
is octaves and `drive` is dB. `--gate` is the note length in seconds; `--velocity`
sets its intensity. The renderer includes the release tail and protects existing
recordings from overwrite. The voice processes drive/filter saturation at four
times the output rate and low-pass filters before downsampling.

`open.voice` changes only the cutoff; `driven.voice` changes only the drive.
These are component studies, with no claim of an approved musical sound.
The existing station retains its original bass voice while this instrument is
developed independently. `music::BassVoice` exposes `note_on`, `note_off`,
`patch` and an allocation-free `render` for later sequencer integration.

### Kick

The standalone kick separates the tuned body, falling pitch, amplitude decay,
noise click and drive. Render several strikes with enough space to hear each tail:

```sh
./build/music-sound kick sounds/kick/round.voice recordings/kick-round.wav --hits 6 --spacing .9
afplay recordings/kick-round.wav
```

`pitch` is the final frequency in Hz; `sweep` is the initial pitch rise in
semitones. `pitch-decay`, `decay` and `click-decay` are the milliseconds taken
for their envelopes to fall by 60 dB; `attack` is the amplitude rise time in ms.
`click` sets transient level, `drive` is dB, and `level` is output gain.
`--velocity` sets strike intensity; `--spacing` is seconds between strikes.
The final tail is included automatically. `tight.voice` changes only the body
decay; `click.voice` changes only the transient amount. Both instruments share
the four-times-rate output filter and DC blocker. The kick supports rapid
retriggers and has no sequencer or model dependency.

### Bass and kick together

The first component study is a sixteen-bar, 116 BPM bass-and-kick groove.
Its four-bar phrase, note lengths, velocities and instrument settings are in
`examples/kick-bass-study.cpp`. The bass sits mostly between kick attacks, with
a short release and mild ducking where notes overlap. Render it and both parts:

```sh
cmake --build build --target music-groove --parallel 2
./build/music-groove recordings/kick-bass-study
afplay recordings/kick-bass-study.wav
```

The renderer also writes `kick-bass-study-bass.wav` and
`kick-bass-study-kick.wav`. These parts include the study's gain and ducking and
sum back to the mix within PCM16 rounding. Existing outputs are protected.

### First arranged track: Side Street

`music-track` develops the study into a 64-bar instrumental at 116 BPM, about
2:14 including its tail. A short plucked hook, hats and a backbeat join the
bass/kick foundation, with an introduction, breakdown, rebuild, return and outro.
The hook uses the same oscillator/filter voice in a higher register; percussion
comes from the existing synth kit. This track is composed and synthesized in C++.

```sh
cmake --build build --target music-track --parallel 2
./build/music-track recordings/side-street-v1
afplay recordings/side-street-v1.wav
```

The same render writes separate `-bass.wav`, `-kick.wav`, `-percussion.wav` and
`-hook.wav` parts, plus `-sections.txt` with cue times. The parts include their mix
gains and effects and sum to the mix within PCM16 rounding. The notes and
arrangement are in `examples/first-track.cpp`; the original two-instrument study
remains available separately. These are working compositions for listening and
sound development.

### Second arranged track: Night Window

`music-night-window` uses the same instrument palette for a new composition at
118 BPM in G minor, about 2:13 long. The bass moves through G, G, E-flat and F;
a falling plucked motif develops into a higher answer. A short bass feature,
breakdown and return create contrast without adding instruments.

```sh
cmake --build build --target music-night-window --parallel 2
./build/music-night-window recordings/night-window-v1
afplay recordings/night-window-v1.wav
```

The notes and arrangement are in `examples/night-window.cpp`. The renderer
writes a mix, the same four stems and a section cue sheet, and protects existing
outputs. Side Street remains reproducible with its original renderer.

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
chord voices. `voice` selects `pluck`, `wide`, `soft`, or `grit` with a short
crossfade. `grit` adds a fixed fourth, band-pass filter motion, overdrive, and
short pitch glides. `rhythm` selects `steady`, `drive`, `build`, or `broken`
(syncopated kicks). Ducking follows actual kick triggers.
These are discrete controls that can use `at`.

`lead-midi` and `bass-midi` accept sixteen absolute MIDI notes (24..96), `-`
rests, and `~` ties. They allow chromatic, independent riffs without following
the chord progression. For example, `lead-midi 65 ~ ~ ~ 65 ~ ~ - 63 ~ 65 ~ 68 ~ ~ -`.
The existing `melody`, `bassline`, and `bassnotes` commands restore chord-relative
patterns. A manual pattern change cancels future patterns for the same instrument,
including patterns written in the other notation.
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
  -DAI_MUSIC_MLX_ROOT="$(.deps/native-python/bin/python -c 'import sysconfig; print(sysconfig.get_path("platlib") + "/mlx")')"
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

### Check GPU access

If Metal fails to initialize, run this from a regular Terminal to compare GPU
access with the failing process:

```sh
python3 scripts/check_gpu.py
```

The script compiles a tiny Metal kernel and verifies its calculation. If that
passes, it uses the existing `build-ai/ai-music` executable and cached models to
render six seconds of AI audio on Metal. It checks the recording and saves the
logs, timing, audio, and `report.json` under `validation/gpu-check-*`. This is an
offline diagnostic; it does not start playback or establish live performance.
Use `--probe-only` to skip the model render. The script also works from another
directory when called by its absolute path.

The probe distinguishes missing GPU access from an inference or recording
failure. Its short offline render includes startup time and does not establish
sustained real-time performance. Keep the generated report when comparing
execution environments.

### Measure and validate live inference

The inference path caches calculations that depend only on fixed model weights
before compiling the streaming graph. Prompts and recurrent state remain dynamic.
Model loading also leaves the generator stopped until the initial prompt and
buffer settings are ready, avoiding a discarded startup run.

Build and run the performance comparison in a Terminal with Metal access:

```sh
cmake --build build-ai --target ai-music music-benchmark --parallel 2
python3 scripts/tune_live.py --play
```

The script compares the uncached graph, cached constants, and cached constants
with larger Metal command batches. Each configuration gets 25 warmup frames and
250 timed frames. Each frame produces 40 ms of audio; startup time is excluded
from the reported throughput. CSV files break down input preparation, graph
construction, evaluation/waiting, and audio copying. Power settings are captured
in the report directory; the script does not change them. Low Power Mode was
enabled on AC power in the 2026-10-01 inspection, so test with it disabled to
measure normal performance.

If a configuration generates faster than playback, the script runs a two-minute
CoreAudio test with two encoded prompt updates, complete 48 kHz stereo PCM16 WAV
validation, and zero allowed underruns, invalid samples, or recording drops.
The test checks the Metal backend identity, accepted style commands, new prompt
encoding results, and audio progress after each update. It uses the same initial
style as subsequent playback and saves each update's observed completion latency.
The native output callback actually runs; `--silent-output` zeroes only the
speaker samples after the generated audio has been recorded. The test requires
valid continuous callback timestamps, no callback errors or overruns, and no
reported device overloads with monitoring active. Finite native runs stop
recording at the exact requested frame count.
Only a passing CoreAudio test enables continuous speaker playback with `--play`.
Omitting that flag keeps speaker output silent. The separate `--no-audio`
software sink retains its deadline counters (`sink_deadline_misses` and
`sink_max_late_ms`); those do not measure the native audio callback. Validation failures
remain active under Python's `-O` mode and are retained in `report.json`.
This establishes the measured run, not subjective musical quality or 24-hour
reliability. If every configuration is too slow, the script saves profiles and
reports failure.
Evidence is retained under `validation/live-tuning-*`.

### Offline AI when Metal is unavailable

The GPU executable checks for an accessible Metal device before loading the
model. If it reports no device, a separate CPU executable can generate short
recordings. The CPU path runs the actual Magenta model and requires `--offline`.
It is substantially slower than playback and uses more memory for unpacked
weights; it is intended for experiments and learning.
The 2026-10-01 CPU test produced six seconds of 48 kHz stereo audio in 312.58
seconds with no counted audio gaps, invalid samples, or recording drops; peak
process memory was about 3.6 GiB. The output is `recordings/lesson-01-ai.wav`,
with timing and signal checks in `validation/metal-fix/final-generation.json`.

After configuring `build-ai` above, build the optional CPU runtime:

```sh
git clone --branch v0.31.1 --depth 1 https://github.com/ml-explore/mlx.git .deps/mlx-cpu-src
cmake -S cmake/mlx-cpu -B build-mlx-cpu \
  -DAI_MUSIC_MLX_SOURCE="$PWD/.deps/mlx-cpu-src" \
  -DCMAKE_BUILD_TYPE=Release -DMLX_USE_CCACHE=OFF \
  -DCMAKE_INSTALL_PREFIX="$PWD/.deps/mlx-cpu"
cmake --build build-mlx-cpu --target mlx --parallel 2
cmake --install build-mlx-cpu
cmake -S . -B build-ai -DAI_MUSIC_MLX_CPU_ROOT="$PWD/.deps/mlx-cpu"
cmake --build build-ai --target ai-music-cpu --parallel 2
./build-ai/ai-music-cpu --source magenta --offline --duration 6 --no-input \
  --style "instrumental electronic funk, gritty synth bass, 112 BPM" \
  --record recordings/first-cpu-ai.wav
```

An existing MLX v0.31.1 source checkout can be passed as `AI_MUSIC_MLX_SOURCE`.
This runtime remaps imported GPU streams to CPU, expands the model's fused
normalization/attention/dequantization operations, and reuses constant matrix
weights with Accelerate. Recurrent state remains an input to every frame.
The separate `libmlx_cpu` library keeps those adaptations out of the Metal path.

Numerical import checks use tiny exported GPU graphs, so they need no GPU or
model download. They cover normalization, attention masks/sinks, dequantization,
and matrix multiplication with both constant and variable weights:

```sh
cmake -S cmake/mlx-cpu -B build-mlx-cpu -DAI_MUSIC_CPU_IMPORT_TESTS=ON
cmake --build build-mlx-cpu --target mlx_cpu_import_tests --parallel 2
ctest --test-dir build-mlx-cpu --output-on-failure
```

### Generation and live controls

Run commands from the repository directory, or pass an absolute `--model-dir`
path. The first Metal model start compiles GPU kernels and can take longer than later
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
resilience; Metal playback uses a 5,000 ms buffer, primed before model audio
begins. The CPU offline host retains 160 ms. `AI_MUSIC_BUFFER_MS=120..5000`
overrides the capacity for diagnostics. Style changes take effect after prompt
encoding, generation, and the queued audio have reached the output.

The Metal host requests 1 GiB of MLX GPU-memory residency capacity before loading
the model to reduce eviction under memory pressure. This is a process-local
request on supported devices, not a guarantee against competing GPU work.
`AI_MUSIC_WIRED_MB=0` disables it; values from 0 to 2048 override the request.

Native output starts with silence while the model primes its queue, then begins
consuming music at an audio callback boundary. Status retains cumulative audio
device/callback counters and separates preparation (`audio_warmup_*`) from
model playback (`audio_active_*`). Preparation never consumes or records model
audio; any error after playback activation still fails validation.

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
python3 tests/playback_timing.py build/ai-music
python3 tests/live_tuning.py
python3 tests/stream_check.py
python3 tests/web_station.py
python3 tests/web_station.py --program funk-study
# Optional: ten-minute native AI callback check; speakers silent, recording preserved.
python3 scripts/check_stream.py --coreaudio
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

The current five-second-buffer neural configuration passed a two-minute native
test on an M1 with both prompt changes and no counted playback errors. Its
ten-minute attempt failed when model generation exhausted the buffer at about
173 seconds. **Sustained neural playback remains experimental.** See the
[published validation notes and condensed evidence](docs/validation.md) for
the configurations, limits, and reproduction commands.

The live browser station uses the synth. CI validates the default engine and
tooling without model assets; it does not certify neural playback performance.
Raw recordings and machine-specific diagnostic logs remain local.

Upstream: [Magenta RealTime 2](https://github.com/magenta/magenta-realtime),
[model release](https://huggingface.co/google/magenta-realtime-2).
Upstream code and model assets retain their respective licenses.

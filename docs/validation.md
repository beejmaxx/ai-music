# Playback validation

The procedural synth and neural Magenta source are separate paths. Passing
the synth tests does not establish neural generation speed or musical quality.
CI builds the default macOS host and tests the engine and validation tools;
it does not download model weights or certify live GPU performance.
Functional integration tests on shared CI runners explicitly tolerate and
report software-timer misses while still rejecting all source, recording,
stream, and native errors. Native acceptance tests do not use that allowance.

## Current neural status — 2026-10-03

Hardware: Apple M1 MacBook Air, 16 GiB memory. Native tests use the small
Magenta RealTime 2 model with MLX v0.31.1. Other applications remained running;
these are measurements on a shared development machine, not isolated benchmarks.

| Configuration | Requested run | Result |
| --- | --- | --- |
| 1.5 s buffer, default MLX residency | 120 s | Source underruns around 90 s. |
| 1.5 s buffer, 1 GiB residency request | 120 s | Passed, including both prompt changes. |
| 1.5 s buffer, 1 GiB residency request | 600 s | Source underruns around 173 s. |
| 5 s buffer, 1 GiB residency request (current defaults) | 120 s | Passed, including both prompt changes. |
| Current defaults | 600 s | Source underruns around 173 s; test stopped. |

**The ten-minute uninterrupted neural milestone has not been met.** The source
can average faster than playback while periods of slower generation still
exhaust the reserve. In a separate five-minute diagnostic, generation recovered
after the buffer emptied; the observed work deficit was about 1.8 seconds.
Five seconds was chosen to give that observed slowdown more room, but a later
run encountered a longer deficit. Larger buffers are not a guarantee under
sustained resource contention.

The passing two-minute current-default run produced exactly 5,760,000 stereo
frames at 48 kHz with non-silent audio and zero source underruns, invalid samples,
recording drops, active callback errors, timestamp discontinuities, or device
overloads. The failed long run retained its error counters. Condensed,
path-independent evidence is in [validation-2026-10-03.json](validation-2026-10-03.json).
Sequential runs under changing load do not isolate the effect of residency.

Use `--offline` when an AI recording must wait for every model frame rather
than inserting silence on a missed live deadline. The default browser station
uses the procedural synth.

## Reproduce

Build the neural host and download the assets as described in the README.
Run from the repository root with no `AI_MUSIC_*` or `MLX_*` overrides:

```sh
python3 scripts/check_stream.py --coreaudio --duration 120 --expected-buffer-ms 5000
# Extend a passing configuration:
python3 scripts/check_stream.py --coreaudio --duration 600 --expected-buffer-ms 5000
```

The script records its initial prompt and inherited inference settings. It
changes the prompt at one-third and two-thirds of the requested duration.
It uses the real CoreAudio output callback with hardware output silenced after
the generated music is recorded. It verifies:

- Actual Metal backend and configured buffer size.
- Exact WAV frame count, format, non-silent samples, and playback wall time.
- Zero source underruns, invalid samples, and recording drops.
- Continuous timestamps, active overload monitoring, and zero active native errors.
- Both accepted and encoded prompt updates, with time for queued audio to drain.

Native output starts silently while the model primes. Preparation does not
consume or record model audio. Preparation and active counters are reported
separately, cumulative totals are retained, and any error after activation fails
the test. Reports and recordings go into the ignored `validation/` directory.
Failures remain failures; a diagnostic that continues after an underrun is not
a successful validation.

`AI_MUSIC_FRAME_TRACE=/absolute/path.csv` enables a bounded 20,000-row producer
trace, written after the generation thread stops. It records generation stages,
queue levels, waits, and underruns without per-frame file I/O. Encoding readiness
is not audible response latency: the current five-second queue adds delay to
style changes. These tests also do not assess subjective musical quality.

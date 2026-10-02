# Contributing

AI Music is an experimental macOS music engine. Bug reports, reproducible
performance measurements, documentation fixes, and focused pull requests are
welcome. Source contributions use the repository's Apache-2.0 license.

## Build and test

Install Xcode command-line tools, CMake 3.27+, and Python 3.10+. The default
build needs no model files, Python packages, or cloud credentials.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
python3 tests/stream_check.py
python3 tests/live_tuning.py
python3 tests/gpu_check.py
python3 tests/live_controls.py build/ai-music
python3 tests/web_station.py
```

The README covers the optional Magenta/MLX build. Keep the dependency pins and
patches reproducible. Changes to stereo queues, audio callbacks, or prompt
workers need regression coverage for the affected behavior. Keep allocation,
file I/O, inference, and blocking locks out of the native audio callback.

## Changes and reports

Open an issue describing the current behavior, expected behavior, reproduction
steps, macOS version, hardware, build command, and relevant errors. For playback
problems, include the source, buffer settings, and final status counters.
Remove credentials and personal paths from logs before sharing them.

Keep pull requests focused. Explain the problem, resulting behavior, and checks
run. State when a test was not run. Include measured evidence for performance
claims; faster average generation alone does not prove uninterrupted playback.
The [validation notes](docs/validation.md) explain the native acceptance test.

Generated audio, downloaded weights, build products, and private working notes
belong in the ignored local directories, not pull requests. Preserve the
attribution and license information of any code or assets you contribute.

Treat other contributors respectfully. Discuss the code and evidence rather
than the person. Report vulnerabilities privately as described in
[SECURITY.md](SECURITY.md).

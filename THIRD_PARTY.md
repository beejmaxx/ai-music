# Third-party code and model assets

The project source is [Apache-2.0](LICENSE). That license does not replace
the licenses of downloaded dependencies, model weights, or reference music.

| Component | Source/version | License |
| --- | --- | --- |
| Magenta RealTime 2 C++ engine | [Google, commit `694a545`](https://github.com/magenta/magenta-realtime/tree/694a545e4ba0b88bf1150137b129582166d3e07f) | [Apache-2.0](https://github.com/magenta/magenta-realtime/blob/694a545e4ba0b88bf1150137b129582166d3e07f/LICENSE) |
| MLX | [Apple, v0.31.1](https://github.com/ml-explore/mlx/tree/v0.31.1) | [MIT](third_party/licenses/MLX.txt) |
| TensorFlow Lite | [v2.21.0](https://github.com/tensorflow/tensorflow/tree/v2.21.0/tensorflow/lite) | [Apache-2.0](https://github.com/tensorflow/tensorflow/blob/v2.21.0/LICENSE) |
| SentencePiece | [v0.2.0](https://github.com/google/sentencepiece/tree/v0.2.0) | [Apache-2.0](https://github.com/google/sentencepiece/blob/v0.2.0/LICENSE) |

CMake downloads these dependencies when the optional neural backend is enabled.
Their source distributions retain their license files and those of their
transitive dependencies. This repository does not vendor their complete source
trees or distribute prebuilt neural binaries.

## Local modifications

The `magenta-*.patch` files modify the pinned Google engine for prompt-worker
lifetime, stereo queue correctness, host metrics, scheduling, graph constants,
bounded tracing, and configurable buffering. `mlx-cpu-import.patch` and
`cmake/mlx-cpu/` provide the optional CPU import path. See
[the patch inventory](patches/README.md). Upstream copyright notices are retained.

## Model weights

Magenta RealTime 2 is by Google DeepMind, Copyright 2026 Google LLC. Its model
weights use **Creative Commons Attribution 4.0 International**, with additional
usage information in the [upstream model card](https://github.com/magenta/magenta-realtime/blob/694a545e4ba0b88bf1150137b129582166d3e07f/MODEL.md).
The [download script](scripts/download_models.py) fetches model assets from
Google's public bucket; weights are not included in this repository. Consult
the model card when using or redistributing those assets and generated outputs.

The `funk-study` procedural program is a sound-design study referencing Daft
Punk's “Da Funk”; it does not load or sample a commercial recording. References
are linked in the README. The software license grants no rights to the
referenced recording or musical composition. User reference audio remains local.

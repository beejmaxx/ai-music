# Dependency patches

These files are AI Music modifications, not unmodified upstream releases.
They retain upstream license and copyright information; see
[THIRD_PARTY.md](../THIRD_PARTY.md) and [NOTICE](../NOTICE).

`cmake/Magenta.cmake` applies the following patches in order to Magenta RealTime
commit `694a545e4ba0b88bf1150137b129582166d3e07f`:

| Patch | Purpose |
| --- | --- |
| `magenta-owned-prompt-worker` | Own and join the prompt worker and handle prompts queued while it finishes. |
| `magenta-metrics` | Expose safe host metrics. |
| `magenta-worker-qos` | Request user-initiated scheduling for generation. |
| `magenta-stereo-read` | Keep left/right consumption aligned. |
| `magenta-live-performance` | Cache constant graph outputs, expose stage timing, and control model autostart. |
| `magenta-stereo-write` | Publish stereo frames only when both channels have capacity. |
| `magenta-prompt-log` | Serialize prompt evidence with host logs. |
| `magenta-producer-trace` | Collect bounded, deferred producer traces and expose the keepalive diagnostic. |
| `magenta-buffer-capacity` | Expand the bounded ring and prime complete frames for the configured capacity. |

The CMake integration validates patch prefixes on copies of the five affected
files before applying missing patches. Do not discard unrelated upstream
checkout edits to make a patch apply.

`mlx-cpu-import.patch` targets MLX v0.31.1 and is applied only by the separate
`cmake/mlx-cpu/` build. It allows the optional offline CPU runtime to lower
operations from the exported GPU graph. The regular Metal runtime is separate.

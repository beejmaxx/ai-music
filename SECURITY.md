# Security

This project is experimental. Security fixes are developed on the `main`
branch; there are no maintained release branches yet.

Report vulnerabilities through
[GitHub private vulnerability reporting](https://github.com/beejmaxx/ai-music/security/advisories/new).
Include affected versions, reproduction steps, and impact. Do not put working
credentials or private audio in a public issue.

The browser station binds to `127.0.0.1` and provides local playback controls
without user authentication. Its same-origin and host checks are intended for
local use. Internet-facing deployment and multi-user authorization are outside
the current design. Model downloads are pinned by size and checksum; this
repository does not distribute model weights or require cloud API keys.

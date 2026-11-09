# Private full-game CI checks

`Private full game build checks` is manually dispatched from `main`. It builds
and executes the complete executable on macOS Intel, macOS arm64 and Windows
x86-64, with SKSE/SkyUI and FFmpeg enabled. Unlike the public source-only checks,
it requires generated game inputs from your own Skyrim installation.

Prepare the private input from a working generated tree:

```sh
python tools/ci_game_input.py pack
```

Upload `build/private-ci/skydream-game-input.tar.gz` to a private HTTPS server.
Use a short-lived signed URL or a dedicated random access token and disable
public directory listing. Do not commit this archive or paste its access URL
into an issue, workflow input or build log. In GitHub repository Settings →
Secrets and variables → Actions, create repository secrets:

- `SKYDREAM_GAME_INPUT_URL`: direct HTTPS download URL (no HTML login page).
- `SKYDREAM_GAME_INPUT_SHA256`: the 64-character digest printed by the tool.

The downloader verifies the digest before extraction and accepts only regular
C/header/binary files under `gen/`. It rejects links, duplicate entries, path
traversal and oversized archives. HTTPS errors do not print the download URL.
Use a fresh archive after regenerating code; CI deliberately does not cache it.

Run the workflow manually on `main`. It has no artifact/cache upload steps:
generated source, embedded game data and resulting executables remain private
on ephemeral runners and are removed in an `always()` cleanup step. Anyone
allowed to edit workflows or run code with repository secrets can access these
inputs; do not grant that access to untrusted contributors.

The `embedded-game` CTest runs `lb --embedded-smoke` without a window or game
assets. It reserves the compatibility address space, initializes shims/thread
state, loads the real embedded image and its imports, checks linked function
tables, and runs the retained recompiled TESV `strchr` implementation against
four cases. This checks complete linking and a small amount of actual generated
code execution, not game startup, save loading, GPU rendering or gameplay.

A successful run does not produce a playable distribution. Full graphics still
needs target-OS vkd3d-shader 2.0 and, on macOS, Vulkan/MoltenVK. Those dependencies
are not used by this headless check. Packaging and graphics validation remain
separate work.

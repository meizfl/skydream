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

For encrypted output also configure `SKYDREAM_ARTIFACT_KEY`: base64 encoding of
32 cryptographically random bytes. Keep an offline copy; GitHub cannot display
secret values later. Never include the key in an artifact.

The downloader verifies the digest before extraction and accepts only regular
C/header/binary files under `gen/`. It rejects links, duplicate entries, path
traversal and oversized archives. HTTPS errors do not print the download URL.
Use a fresh archive after regenerating code; CI deliberately does not cache it.

Run the workflow manually on `main`. Successful tested installs are compressed
and encrypted with AES-256-GCM (random 96-bit nonce, 128-bit authentication tag).
Only `encrypted/*.tar.gz.enc` is uploaded, with seven-day retention. There is no
plaintext artifact/cache upload. Private inputs, staging files and executable
build products are removed in an `always()` cleanup step. Anyone allowed to
edit workflows or run code with repository secrets can access those inputs;
do not grant that access to untrusted contributors.

To decrypt a downloaded artifact (use the script from the matching commit):

```sh
python -m pip install cryptography==50.0.2
python tools/ci_encrypt_artifact.py decrypt Skydream-macOS-arm64.tar.gz.enc Skydream.tar.gz --key-file /private/path/artifact.key
tar -xzf Skydream.tar.gz
```

The decoder authenticates the entire payload before creating plaintext, refuses
to overwrite existing output, and creates the decrypted file with private
permissions. The `.enc` format consists of `SKYDREAM-ARCHIVE-1\n`, a 12-byte nonce,
and AES-GCM ciphertext with its tag; the magic is authenticated associated data.

The archive includes root-level `Skydream` / `Skydream.exe`, DXVK, SDL2,
FFmpeg, target-native vkd3d-shader and the non-system dependency closure. Mac
packages also include Vulkan Loader, MoltenVK and an ICD manifest. Windows
packages include Vulkan Loader but use the GPU vendor's installed ICD/driver.
Install requires no Homebrew/MSYS2 on the destination system. Dependency
identities, binary hashes and available license files are under `share/`.

The `--bundle-smoke` check executes native HLSL compilation and loads the
packaged Vulkan libraries without creating a graphics window. Windows runs it
with MSYS2 removed from PATH; macOS checks the loaded dylibs for Homebrew paths.

The `embedded-game` CTest runs `lb --embedded-smoke` without a window or game
assets. It reserves the compatibility address space, initializes shims/thread
state, loads the real embedded image and its imports, checks linked function
tables, and runs the retained recompiled TESV `strchr` implementation against
four cases. This checks complete linking and a small amount of actual generated
code execution, not game startup, save loading, GPU rendering or gameplay.

A successful run validates a relocatable dependency bundle and a small amount
of generated game logic. It does not prove correct rendering/gameplay on a
physical target GPU. Windows still requires a compatible graphics driver;
macOS builds target macOS 15+ and use ad-hoc signing, not notarization.

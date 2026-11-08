# Native module replacement: first milestone

The aim is to remove the remaining PE memory-image dependencies one module at
a time. This is separate from static recompilation of TESV itself.

| Component | Execution / data source | PE image still required |
| --- | --- | --- |
| TESV 1.9.32 | Statically recompiled Linux code, original memory image | Yes |
| D3DX9 helper | Statically recompiled code, original memory image | Yes |
| Bink video/audio | Native FFmpeg decoder and SDL audio, stdcall adapter | **No** |
| X3DAudio | Native F3DAudio C math and 32-bit guest ABI adapter | **No** |

## X3DAudio

`X3DAudioInitialize` and `X3DAudioCalculate` imports now resolve to native cdecl
shims. The old DLL is neither opened nor mapped, its DllMain is not run, and
`gen/x3da_*.c` is excluded even if left over from an older build. Regeneration
and packaging require only TESV, D3DX9 and Bink. Existing X3DAudio files in old
packs are ignored and may be removed after updating the runtime.

The native implementation is F3DAudio from FAudio 26.10, commit
`6839b88e304a046ae1a609ff14a085371e02a4e0`, under its original zlib license.
Only the spatial calculation module is included: there is no new SDL3,
FAudio shared-library, Wine or PE dependency. See
`third_party/f3daudio/README.skydream` for the small platform adaptation.
The existing SDL/XAudio2 mixer continues to play the resulting sound.

The bridge translates all pointers explicitly: listener/emitter cones,
channel azimuths, five distance curves, curve points and the output matrix.
Guest structures remain 52 / 100 / 48 bytes regardless of host pointer size.
It preserves scalar outputs that were not requested by the calculation flags.

## Validation (2026-11-09)

- 14 local regression tests passed, including spatial rotation, attenuation,
  Doppler, custom curves, stereo, 5.1 and invalid pointer boundaries.
- A private test install contained exactly TESV, D3DX9 and Bink guest images.
  The game reached gameplay and executed native spatial calls with flags
  `0x3d`, using a 5.1 speaker mask `0x3f`; SkyUI remained enabled.
- An optional Wine test oracle called the user-owned original DLL on the same
  18 synthetic inputs. Wine is used only by this offline comparison tool.
  Maximum absolute error for mono emitters was `1.1920929e-7` in stereo and
  `1.49011612e-7` in 5.1, over six positions for each configuration.
- Two-channel spatial emitters showed matrix-coefficient differences up to
  `0.0411653072`. This remains a known upstream algorithm difference; the
  adapter does not promise bit-for-bit X3DAudio equivalence.

Run ordinary tests without any original DLL:

```sh
python3 -B -m unittest discover -v -s tools -p 'test_*.py'
```

Optional reference comparison (requires Wine and 32-bit MinGW, test-only):

```sh
python3 tools/check_x3daudio_reference.py /path/to/original/X3DAudio1_7.dll \
  --wine-prefix /path/to/dedicated/test-prefix --output /tmp/x3da-comparison.json
```

## Limits

F3DAudio does not implement `CALCULATE_DELAY`. The bridge rejects that flag
explicitly rather than silently supplying fake delay values. It was not used
in the exercised Skyrim path. The bridge supports up to 64 source channels
and 8 destination channels, matching the current mixer's output limit.
Listening tests and more gameplay, especially multichannel spatial sources,
remain useful; these tests do not establish perfect audio parity everywhere.

## Bink replacement (2026-11-09)

Skyrim imports BinkOpen, BinkDoFrame, BinkCopyToBufferRect, BinkNextFrame,
BinkWait, BinkPause, BinkClose, BinkSetSoundSystem and BinkOpenDirectSound.
These are native stdcall shims; BinkCopyToBuffer is also available for the
standalone diagnostic. BinkOpenDirectSound/SetSoundSystem select the native
SDL sound path; no DirectSound DLL or guest callback is needed.

The guest HBINK prefix contains dimensions, frame count, 1-based frame index
and rational frame rate. The host owns demuxer/codec state and audio queues.
BGRA copies honor source/destination coordinates, pitch and partial 256x256
tiles. BinkWait uses monotonic timing; pause freezes both audio and video time.
Closing releases decoders, PCM/video buffers, SDL device and guest handle.

The loader, generated-source filter, regeneration and input manifests now
require only TESV and D3DX9 PE images. Bink PE files left in an old pack are
ignored. The `.bik` movie itself remains original game data and is still needed.

Validation: BGS_Logo.bik decoded all 166 frames at 1280x720 and 24000/1001 fps,
including 614400 stereo float samples at 44100 Hz. Selected frame hashes and
the complete decoded audio matched the FFmpeg CLI byte for byte on the host.
The game played all frames in 6883 ms, queued audio to a real SDL device, and
the tester confirmed visible logo playback after partial-tile clipping was
fixed. Native framebuffer capture also confirmed the rendered logo.

The ordinary suite additionally checks codec/demuxer availability, invalid
headers and partial-tile bounds. Set `SKYDREAM_BINK_TEST_MOVIE=/path/to/movie.bik`
to enable private real-media verification; no movie is included in the repo.

Linux builds now need libavformat and libswscale development packages in
addition to libavcodec/libavutil/libswresample. A minimal FFmpeg 4.3 configure
must enable `--enable-decoder=bink,binkaudio_dct,binkaudio_rdft` (the decoder
selector is **bink**, not binkvideo), `--enable-demuxer=bink`,
`--enable-protocol=file`, avformat and swscale. Portable packages must bundle
libavformat.so.58 and libswscale.so.5 as well.

Scope: sequential Bink 1 movies with their first audio track and BGRA output.
This is not the entire Bink SDK: Bink 2, seeking/loop control, custom I/O,
custom sound callbacks and other unregistered exports are not implemented.
Android builds without FFmpeg cannot play movies through this decoder.

Portable validation: the Debian 11 minimal FFmpeg build passed all 18 tests
with the private movie enabled. Its scalar colorspace converter differed from
the full system CLI's SIMD converter by at most 2 RGB quantization levels
out of 255 on the selected frames; decoded audio matched byte for byte.
The test enforces this bound, verifies dimensions and exact PCM, and checks
that required decoders/demuxer are actually present in minimal FFmpeg builds.

D3DX9 helper replacement is described in [NATIVE_D3DX.md](NATIVE_D3DX.md). Skyrim now needs only TESV.exe.pe as a guest memory image.

The external TESV image has also been removed from runtime requirements; see [EMBEDDED_GAME.md](EMBEDDED_GAME.md).

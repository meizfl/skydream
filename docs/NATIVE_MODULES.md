# Native module replacement: first milestone

The aim is to remove the remaining PE memory-image dependencies one module at
a time. This is separate from static recompilation of TESV itself.

| Component | Execution / data source | PE image still required |
| --- | --- | --- |
| TESV 1.9.32 | Statically recompiled Linux code, original memory image | Yes |
| D3DX9 helper | Statically recompiled code, original memory image | Yes |
| Bink video | Statically recompiled code, original memory image | Yes |
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

# Experimental classic SKSE compatibility

This is an independent implementation of a small part of the classic SKSE
Papyrus interface for TESV 1.9.32. It is **not a port of the complete SKSE
runtime** and does not load `skse_1_9_32.dll` or `skse_loader.exe`.

Enable it explicitly:

```sh
JOBS=16 LB_GEN_JOBS=16 sh desktop/build.sh -DSKYDREAM_SKSE=ON
```

For an experimental SkyUI build that needs no launch environment variable:

```sh
JOBS=16 LB_GEN_JOBS=16 sh desktop/build.sh -DSKYDREAM_SKSE=ON -DSKYDREAM_SKYUI=ON
```

This enables the partial 1.7.3 interface by default, not Windows DLL plugins
or complete SKSE support. The ordinary compatibility variant still reports
0.1.0 unless its diagnostic switch is set.

Disable both options with `-DSKYDREAM_SKYUI=OFF -DSKYDREAM_SKSE=OFF` (the default). The option persists in the
CMake build directory until changed. `Skydream --help` reports the variant.
The Linux link step wraps the original Papyrus registration symbol. Three Scaleform entry points use host overrides prepared by
`tools/prepare_skse_hooks.py`; both build variants retain guest fallbacks. Cross-module calls must remain link-visible (the current build does not
use LTO).

## Implemented interface

90 non-latent registrations, including static functions and Form/Alias/ActiveMagicEffect methods:

- `Math`: LeftShift, RightShift, LogicalAnd, LogicalOr, LogicalXor,
  LogicalNot, Log.
- `StringUtil`: GetLength, GetNthChar, IsLetter, IsDigit, IsPunctuation,
  IsPrintable, AsOrd, AsChar, Find, Substring. `Split` is not implemented.
- `ColorComponent`: GetAlpha, GetRed, GetGreen, GetBlue, GetHue,
  GetSaturation, GetValue, SetAlpha, SetRed, SetGreen, SetBlue, SetHue,
  SetSaturation, SetValue.
- `SKSE`: GetVersion, GetVersionMinor, GetVersionBeta, GetVersionRelease,
  GetPluginVersion. These report **0.1.0 / release 0**, not SKSE 1.7.3;
  plugin lookup returns -1 because there is no external plugin loader.

Additional interfaces include UI Get/Set/Invoke primitives and arrays,
menu/key/mod-event subscriptions, Form.GetType, input mapping and numeric INI
access. Scaleform receives a native `skse` object, extended inventory type,
weapon/armor/keyword data, text input, remapping and menu callbacks. Active
plugin translation dictionaries are loaded through the game's resource API.

Version-dependent mods may correctly reject this incomplete implementation.
`LB_SKSE_SKYUI_PROBE=1` is a development-only diagnostic that advertises the
1.7.3 interface to exercise SkyUI beyond its gate. It is not certification of
SKSE 1.7.3 compatibility and is disabled by default. ColorComponent.GetAlpha
extracts the alpha byte; it does not reproduce the original registration's
apparent GetRed alias.

Existing native functions remain registered by the original game. Additional
functions are registered immediately afterward through the game VM registry.
Guest native-function objects, vtables and VMValues keep 32-bit layouts;
callbacks dispatch to compiled host C functions through runtime thunks. The
bridge uses the game's argument accessors, allocator and string cache, and
preserves the surrounding guest CPU state. Classic Papyrus strings are
case-insensitively interned; string indexing is byte-based.

## Validation

```sh
python3 -m unittest discover -v -s tools -p 'test_skse*.py'
LB_SKSE_SELFTEST=1 ./Skydream /path/to/private/game /path/to/test/profile
```

The optional integration probe invokes the 36 primitive natives through their
actual guest vtable, using real game argument accessors and string cache. It
runs at VM initialization and logs `primitive guest ABI probes passed` on
success. System calls require separate gameplay tests. This validates the bridge, not compatibility with an arbitrary mod.

Mods also need their Papyrus declarations (`.pex`). Use scripts from your own
classic SKSE installation in an isolated game folder for testing. They are not
included in this source tree or public artifacts. Installing all original SKSE
scripts does **not** implement the remaining native functions.

## SkyUI 5.1 test

Tested with the original LE archive in an isolated game folder and a copy of a
save. In diagnostic mode, inventory lists render with correct type icons and
Russian translations; MCM registers SkyUI and opens its settings pages. Native
Wayland is confirmed by the platform log. These are partial gameplay results,
not a declaration that all SkyUI features work. The tester confirmed that MCM settings persist. Favorites, active effects
and all crafting menus still require validation.

A subsequent Papyrus log exposed division by zero in ApplySettings: INI lookup
mistakenly tested all of EAX for a guest bool returned only in AL, preventing
fallback from Skyrim.ini to SkyrimPrefs.ini. The lookup now masks AL and
requires a non-null result before stopping the search.

## Not implemented

Windows DLL plugins, native Linux plugin ABI, SKSE serialization/co-saves,
most game/form/actor and array extensions, active-effect Scaleform callbacks,
favorites index storage and several crafting/map hooks remain incomplete.
Default version reporting deliberately remains 0.1.0 / release 0 while these
compatibility tests are ongoing.

An inventory can be generated from a user-provided SKSE source distribution:

```sh
python3 tools/audit_skse.py /path/to/SKSE --output /tmp/skse-audit.json
```

The provided 1.7.3 source has 798 literal native registrations across 60 class
names and 69 detected patch expressions. This is a static inventory, including
conditional registrations, not a runtime coverage count.

## Source and distribution boundaries

The supplied SKSE package includes sources, but its README says modified
versions must not be released without contacting the authors and prohibits
rehosting the package. `src/skse/skse_license.txt` also contains a separate
restriction. `src/common` has a different permissive notice. Consequently this
project does not copy SKSE implementation code, scripts, or binaries, and does
not place them under GPL. Interface names and verified guest ABI locations are
used for independent interoperability work. This compatibility layer must not
be described as the official SKSE project.

## Direct portable launch

The executable locates game files beside itself regardless of the working
directory. If `portable-profile/` exists beside the game binary, it uses that
profile for INIs, plugin lists and saves; an explicit SAVE_DIR takes precedence.
Without this directory, the usual XDG save location remains in use. Private
full packages can include a copied profile; never distribute personal saves
or Bethesda/SKSE/SkyUI assets as part of the public source artifact.

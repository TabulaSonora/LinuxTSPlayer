# Tabula Sonora Player

A GTK4 / Libadwaita front end for [NativeTS](https://github.com/TabulaSonora/NativeTS), a C++
reimplementation of the Roland Sound Canvas VA synthesizer voice.

It is a Linux port of the SwiftUI [Apple front end](https://github.com/TabulaSonora/AppleTSPlayer),
and reaches the same feature set: transport with seeking and looping at the file's own loop points,
a mixer strip per part the file addresses, engine settings, and WAV export through the library's own
writer so the bytes match `tabula-sonora render`.

Beyond the Apple build it adds the things a Linux desktop expects: MPRIS2, ALSA sequencer MIDI in,
drag and drop, a recent-files menu, and an adaptive layout.

The layout switches on **height**, not width: the wide form stacks the transport above the mixer, so
what runs out first is vertical room — a short window leaves the mixer two rows tall and useless
while the transport is still perfectly comfortable. Below 460sp tall the two move into a view
switcher instead. Horizontally the stacked layout stays usable down to the 360px minimum.

It plays far more than SMF — RIFF-MIDI, DirectMusic `MIDS`, DOOM `MUS`, Miles `XMI`, `GMF`, both HMI
containers, Mobile XMF and LDS tracker files — because the engine converts all of them to SMF on the
way in.

## Installing

A Flatpak bundle is attached to each [release](https://github.com/TabulaSonora/LinuxTSPlayer/releases).
Releases marked as prereleases and tagged `main-<sha>` are automatic builds of the latest commit;
anything else is a tagged version.

```sh
flatpak install --user tabula-sonora-player.flatpak
flatpak run co.losno.TabulaSonoraPlayer
```

Flatpak rather than a `.deb` or `.rpm` for a concrete reason. The interface needs libadwaita 1.6 for
`AdwMultiLayoutView`, `AdwSpinner` and the accent-colour API, and two distributions people actually
run cannot supply it: Ubuntu 24.04 LTS ships 1.5.0 and is supported until 2029, and Debian 12 ships
1.2.2. Native packages for those could not be built at all. The GNOME runtime carries its own
libadwaita, so one bundle reaches every distribution — and there is one packaging format to maintain
instead of three.

Arch users can build the package in `packaging/arch/` instead, which links against the system
libadwaita.

Two permissions the bundle asks for, and why:

- `--device=all`, because the ALSA sequencer is at `/dev/snd/seq` and Flatpak has no narrower grant
  that reaches it. Revoke it with `flatpak override --nodevice=all co.losno.TabulaSonoraPlayer` and
  everything except playing from a MIDI keyboard still works.
- `--filesystem=host:ro`, because dragging a file onto the window and naming one on the command line
  both hand over a bare path rather than a portal document, and music does not live in `~/Music` —
  it lives on whichever disk the collection was ripped to. Nothing is written outside the sandbox:
  exports go through the save portal and the ROM is copied into the application's own data
  directory.

## The ROM

**The engine is inert without `SCCore.dll`** from a licensed Roland SOUND Canvas VA 1.1.6 install:
exactly 27,347,456 bytes, SHA-256 `117e6aa1…c620bdb1`. It is not included and cannot be — it holds
Roland's 24 MB wave ROM. It is read as *data* and never loaded as code. The application asks for it
on first launch, verifies it, and keeps its own copy under `~/.local/share/tabula-sonora/`.

## Building

Dependencies: GTK ≥ 4.14, libadwaita ≥ 1.6, alsa-lib, zlib, plus `miniaudio` and `nlohmann-json`
headers at build time.

```sh
git clone --recurse-submodules https://github.com/TabulaSonora/LinuxTSPlayer.git
cd LinuxTSPlayer
cmake --preset dev
cmake --build --preset dev
GSETTINGS_SCHEMA_DIR=build/dev/data ./build/dev/src/tabula-sonora-player
```

The engine is the `nativets` submodule. `TSGUI_NATIVETS_DIR` overrides it, and if the submodule was
not checked out the build falls back to a sibling `../NativeTS` before giving up with a message
saying so.

The presets use vcpkg for `miniaudio`, `nlohmann-json` and `cli11`; a plain configure works too if
those are installed system-wide.

### Packaging

`packaging/arch/` has a PKGBUILD. `packaging/flatpak/` has the Flatpak manifest:

```sh
flatpak-builder --user --install --force-clean build-flatpak \
    packaging/flatpak/co.losno.TabulaSonoraPlayer.yml
```

It copies the working tree rather than pulling a git ref, because a release is built from a tag,
where the checkout is a detached HEAD and no branch name resolves. `build/` is excluded.

CI builds the bundle on every push and publishes it. A push to `main` becomes a prerelease tagged
`main-<sha>` covering whatever commits it carried; a `v*` tag becomes a proper release. Only the ten
newest development builds are kept — tagged versions are never pruned — and every run also uploads
the bundle as a workflow artifact.

Only this project is recompiled per commit: the dependency modules are cached, so a warm build takes
seconds rather than minutes.

### Two build rules that are not preferences

- **Never add `-ffast-math`.** The engine's control path is 16-bit fixed point and depends on
  wrapping; its float narrowing guarantees break if the compiler fuses `a*b+c` into an FMA. The
  required `-fwrapv -ffp-contract=off` arrive automatically through `ts::tabulasonora`.
- **Do not run a Debug build unoptimised.** The synth renders only ~1.4× realtime that way, which no
  ring can absorb. `TSGUI_FAST_DEBUG` (on by default) compiles the engine at `-O2` even in Debug.

## Translating

The interface, the desktop entry, the AppStream metainfo, the GSettings descriptions and the MIME
comments all come from one catalogue per language under `po/`. Spanish (Chile) and Japanese ship
today.

```sh
cmake --build --preset dev --target update-po   # rescan the sources, merge into every catalogue
```

That runs `update-pot` first, so it both refreshes `po/tabula-sonora-player.pot` and merges it into
each `.po`. Neither target runs as part of a normal build: regenerating the template rewrites its
timestamp, and a no-op build should not leave the tree dirty.

To add a language, put its code in `po/LINGUAS` and create the catalogue from the template:

```sh
msginit --locale=pt_BR --input=po/tabula-sonora-player.pot --output=po/pt_BR.po
```

An uninstalled build has no catalogue under the install prefix, so point the app at the one in the
build tree:

```sh
TS_LOCALE_DIR=build/dev/po/locale LANGUAGE=es_CL GSETTINGS_SCHEMA_DIR=build/dev/data \
    ./build/dev/src/tabula-sonora-player
```

Tone and drum-kit names are not translated anywhere. They come out of `SCCore.dll` at runtime and
are the module's own display text, the same strings its LCD shows; translating them would break the
correspondence with every patch chart. The module designations and the SysEx message names in the
song information are left alone for the same reason.

## Testing

```sh
ctest --preset dev          # needs $TS_SCCORE_DLL and -DTSGUI_TEST_MIDI=... for the engine tests
```

The load-bearing one is `export-matches-cli`: it renders a song through this program's export path
and byte-compares the WAV against `tabula-sonora render`. Both sides are built from the same engine
tree, so the test asks only whether *this program* renders the engine faithfully, and does not turn
red every time upstream changes the voice.

It has already earned its keep. Export originally rendered in quarter-second chunks so it could
report progress, which turns out to change the audio: `SequencePlayer::render` hands the generator
every event due within the span it is given, stamped with the offset it falls at, and the engine
rounds that stamp to the millisecond -- so the span boundaries decide where events land. An export
is therefore a single `render_to_end` call, exactly as the CLI does it, and the progress bar is
indeterminate because there is nothing to report from inside one.

`tools/shoot.sh` screenshots the app on a throwaway Xvfb display, which is how the interface is
checked without disturbing a running session.

## How it fits together

```
ALSA seq ─┐
          ├─► Player ──► Session ──► ToneGenerator ──► FrameRing ──► miniaudio ──► device
GUI ──────┘   (render thread)                            ▲
                                                    (lock-free)
```

`src/host/` is the engine host layer and knows nothing about GTK; `src/app/` is the interface and
never touches the engine except through `TsPlayerModel`.

`ts_session.cpp` and `ts_player.cpp` are vendored from the Apple front end's bridge, which was
already platform-neutral C++ — the port replaced about ten lines of `os_workgroup`/QoS scheduling
with `pthread_setname_np` and a best-effort `SCHED_FIFO`. Everything they encode about locking,
ring lead, and carrying part state across an engine rebuild is upstream's and was left alone.

One deliberate divergence: the Apple build can only sound live MIDI *over a playing song*, because
its render loop sleeps whenever the transport is stopped. Here a stopped transport still renders the
generator (`Session::render_live`), so the instrument is playable on its own without the loaded file
starting to move.

## Licence

BSD 3-Clause. `NOTICE.md` must travel with any binary: reverb and chorus coefficients compiled into
the engine are Roland-derived.

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

## The ROM

**The engine is inert without `SCCore.dll`** from a licensed Roland SOUND Canvas VA 1.1.6 install:
exactly 27,347,456 bytes, SHA-256 `117e6aa1…c620bdb1`. It is not included and cannot be — it holds
Roland's 24 MB wave ROM. It is read as *data* and never loaded as code. The application asks for it
on first launch, verifies it, and keeps its own copy under `~/.local/share/tabula-sonora/`.

## Building

Dependencies: GTK ≥ 4.14, libadwaita ≥ 1.6, alsa-lib, zlib, plus `miniaudio` and `nlohmann-json`
headers at build time.

```sh
cmake --preset dev -DTSGUI_NATIVETS_DIR=/path/to/NativeTS
cmake --build --preset dev
GSETTINGS_SCHEMA_DIR=build/dev/data ./build/dev/src/tabula-sonora-player
```

The presets use vcpkg for `miniaudio`/`nlohmann-json`; a plain configure works too if both are
installed system-wide. `TSGUI_NATIVETS_DIR` defaults to `./nativets`, then to a sibling `../NativeTS`
checkout.

`packaging/arch/` has a PKGBUILD.

### Two build rules that are not preferences

- **Never add `-ffast-math`.** The engine's control path is 16-bit fixed point and depends on
  wrapping; its float narrowing guarantees break if the compiler fuses `a*b+c` into an FMA. The
  required `-fwrapv -ffp-contract=off` arrive automatically through `ts::tabulasonora`.
- **Do not run a Debug build unoptimised.** The synth renders only ~1.4× realtime that way, which no
  ring can absorb. `TSGUI_FAST_DEBUG` (on by default) compiles the engine at `-O2` even in Debug.

## Testing

```sh
ctest --preset dev          # needs $TS_SCCORE_DLL and -DTSGUI_TEST_MIDI=... for the engine tests
```

The load-bearing one is `export-matches-cli`: it renders a song through this program's export path
and byte-compares the WAV against `tabula-sonora render`. If the host layer ever changes the
engine's audio behaviour, that test fails.

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

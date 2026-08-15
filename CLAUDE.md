# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A GTK4 / libadwaita front end (`co.losno.TabulaSonoraPlayer`, binary `tabula-sonora-player`) for
**NativeTS**, a C++ reimplementation of the Roland Sound Canvas VA voice. NativeTS is the `nativets`
submodule and is the engine; this repository is only the host layer and the interface. It is a port
of the SwiftUI [AppleTSPlayer](https://github.com/TabulaSonora/AppleTSPlayer), and much of the design
is deliberately traceable to it — comments say so where it matters.

## Prerequisites that are not optional

- `git submodule update --init --recursive` (or `-DTSGUI_NATIVETS_DIR=/path/to/NativeTS`, or a
  sibling `../NativeTS`, which the build falls back to).
- **The engine is inert without `SCCore.dll`** from a licensed Roland SOUND Canvas VA 1.1.6:
  27,347,456 bytes, SHA-256 `117e6aa1…c620bdb1`. Never committed. Read as data, never loaded as
  code. The app copies a verified one to `~/.local/share/tabula-sonora/SCCore.dll`.
- Engine tests additionally need a MIDI file passed via `-DTSGUI_TEST_MIDI=…`.

## Commands

```sh
cmake --preset dev                 # Debug GUI over an -O2 engine
cmake --build --preset dev
GSETTINGS_SCHEMA_DIR=build/dev/data ./build/dev/src/tabula-sonora-player [file.mid]
```

The `GSETTINGS_SCHEMA_DIR` is required for an uninstalled run — the app aborts without its schema.
`TS_LOCALE_DIR=build/dev/po/locale` alongside it is what makes an uninstalled run translated;
without it the catalogues are looked for under the install prefix and the app stays English.

Other presets: `release` (RelWithDebInfo), `asan`, `tsan`.

```sh
# Tests. Metadata tests run with no ROM; engine tests need both variables at configure time.
cmake --preset dev -DTSGUI_TEST_MIDI="/path/to/song.mid"   # TS_SCCORE_DLL from the environment
ctest --preset dev
ctest --preset dev -R export-matches-cli --output-on-failure   # one test
ctest --preset tsan                # the "ring" label only: host-smoke under ThreadSanitizer

tools/shoot.sh out.png [w] [h] [settle]   # screenshot on a throwaway Xvfb display + profile
                                          # TS_FILE, TS_ROM, TS_ACTION drive what is captured
tools/build-icons.sh                      # regenerate hicolor PNGs from the Icon Composer master

cmake --build --preset dev --target update-po   # rescan strings, merge into every po/*.po

flatpak-builder --user --install --force-clean build-flatpak \
    packaging/flatpak/co.losno.TabulaSonoraPlayer.yml
```

### Tests, and what each is for

- `export-matches-cli` — the load-bearing one. Renders through `Session::plan_export` /
  `run_export` and byte-compares the WAV against `tabula-sonora render` **built from the same engine
  tree**. It asks only whether this program renders the engine faithfully, so it does not go red
  when upstream changes the voice. `TSGUI_BUILD_TESTS=ON` is what makes the engine build its CLI.
- `host-smoke` — producer and consumer across the ring on two threads; both the does-it-make-sound
  test and the TSan test (label `ring`).
- `desktop-file`, `metainfo`, `mime-types` — need `desktop-file-validate`, `appstream-util`,
  `xdg-mime` + `update-mime-database` respectively; each is skipped if its tool is absent.

## Two build rules that are not preferences

- **Never add `-ffast-math`** (or LTO for packaging). The engine's control path is 16-bit fixed
  point and depends on wrapping; its float-narrowing guarantees break if `a*b+c` is fused into an
  FMA. The required `-fwrapv -ffp-contract=off` arrive automatically through `ts::tabulasonora`.
- **Never run the engine unoptimised.** At `-O0` the synth renders ~1.4× realtime and no ring can
  absorb that. `TSGUI_FAST_DEBUG` (ON by default) compiles the engine at `-O2` in Debug while
  leaving GUI sources at `-O0`.

The top-level `CMakeLists.txt` also points `PKG_CONFIG_LIBDIR` at an empty directory across the
engine's `add_subdirectory`, to hide FLAC/Vorbis/Ogg from it — they are only used by its SoundFont
exporter, which this player never calls. If you touch that block, restore by *unsetting* where the
variable was unset: an empty `PKG_CONFIG_LIBDIR` means "search nowhere" and would take gtk4 down
with the codecs.

## Architecture

Three layers, and the boundaries are load-bearing.

### 1. `src/host/` — engine host, no GTK below this line

Namespace `ts::host`, snake_case files and identifiers. Buildable and testable headless (the test
binaries link `tsgui_host` alone).

- **`Session`** (`ts_session.hpp`) owns the engine chain in the order the library builds it —
  `RomImage` → `NoteRenderer` (loads 27 MB of tables) → `ToneGenerator` → `SequencePlayer` — each
  borrowing the one above. Changing a `ToneGeneratorOptions` field rebuilds only the generator, so
  switching vintage or effects costs sounding voices, not a table reload. Not thread-safe by design;
  `Player` gives it one owning thread, which is exactly `ToneGenerator`'s contract.
  **`arm_player` and `run_export` deliberately disagree**, following upstream's own split between
  `apps/audio` and `render`: playback skips the silent lead-in and spreads a dense opening burst
  over control ticks the way a cable would, and an export does neither, because its length and
  alignment are what `export-matches-cli` byte-compares. So a file with an opening bulk dump does
  not export the way it plays — by as much as 2.31 dB — and that is the intended difference. It also
  means "the start" of a song is `Session::start_frame`, its first note, and not sample zero.
- **`Player`** (`ts_player.hpp`) is a session + render thread + `FrameRing`. The render thread pulls
  128-frame blocks and fills the ring to a lead of `latency_ms`; the audio callback only copies out.
  Three deliberately different synchronisation mechanisms:
  - *control* (load, settings, seek, transport) takes `lock_`, which the render thread also holds
    per block — safe because the render thread is not the audio callback;
  - *live MIDI* appends under its own mutex to an inbox swapped wholesale once per block, so a MIDI
    source never waits on a render;
  - *mute/solo* take neither — `ChannelMask`'s flags are atomic for exactly this.
  Lock order where both are needed: `export_lock_` then `lock_`, never the reverse.
- **`AudioDevice`** opens miniaudio at the engine's 32 kHz and lets the server resample.
  **`MidiInput`** is the ALSA sequencer client; it does not throw when `snd-seq` is missing.

### 2. `src/app/` — GObject/libadwaita interface

`ts-*.cpp` files, `TsThing` / `ts_thing_*` GObject naming. C++ throughout: no `G_BEGIN_DECLS`
anywhere, because several headers carry C++ types (spans, references to engine structs) across.

- **`TsPlayerModel` is the only thing that touches `ts::host::Player`.** The render thread
  republishes a `SessionSnapshot` ~50×/s; the model polls it at 10 Hz (`tick_interval_ms`) and turns
  what changed into `notify` signals. Widgets bind to properties and nothing else reaches down.
- **Parts** are always 64 `TsPart` objects (`port * 16 + channel`), created once and updated in
  place so rows do not churn ten times a second. The list never changes length — the mixer filters
  on the `present` property and re-runs the filter on the `presence-changed` signal.
- **GSettings is the source of truth**, not a mirror: the preferences dialog writes keys and
  `ts_settings_bind_model` applies them. One key per field (a later key falls back to its own
  default), whole `TSEngineSettings` applied at once so N changed keys cost one rebuild. Defaults in
  the gschema are the engine's own, so a fresh profile sounds like `tabula-sonora render`.
- **UI is `data/ui/window.ui`**, compiled into a GResource. The layout switches on **height**, not
  width, via `AdwMultiLayoutView`: below 460sp the transport and mixer move into a view switcher.
  One transport and one mixer are built once and moved between layouts.
- **`data/ui/style.css`** is the only stylesheet, loaded from the GResource in
  `ts_application_startup`. It exists for one reason — a mixer strip has to be short enough that all
  sixteen parts fit on screen together — so it is metrics, not colour, and the row's own margins in
  `ts-part-row.cpp` are the other half of the same decision.
- `TsMpris` publishes MPRIS2 on state change only, never on the display tick.
- Actions/accels are registered in `ts-application.cpp`; `win.*` actions live on `TsWindow`.

### 3. `data/` — desktop integration

gschema, desktop entry, metainfo, GResource, icons, and `*.mime.xml` for the legacy container
formats `shared-mime-info` does not know (without it those desktop associations are unreachable).

`.desktop.in`/`.metainfo.xml.in` are `configure_file`d into `gen/`, then `msgfmt --desktop`/`--xml`
merges the catalogues into the copy that installs; `.mime.xml` skips the first stage, having no
placeholders. Edit the `.in`. **No `@VAR@` may appear inside a translatable field** — msgids come
from the source while the merge template is the configured copy, and the two only agree while
substitution never touches translated text. Breaking that drops the translation silently rather
than failing the build.

An XML comment here cannot contain `--`. `glib-compile-schemas` tolerates it, libxml2 does not, and
`xgettext` responds by falling back to its C scanner and extracting *nothing at all* from the file,
with no error.

### 4. `po/` — translations

One catalogue per language plus `LINGUAS` and `POTFILES.in`; domain `tabula-sonora-player`, which
has to agree in four places (`bindtextdomain` in `main.cpp`, `TS_()` in the host, `gettext-domain`
on the gschema's `<schemalist>`, and the `.mo` install path). `update-pot`/`update-po` are
maintainer targets kept out of `ALL`, because regenerating the template rewrites its timestamp and
would dirty the tree on a no-op build. `.mo` compilation *is* in the build.

The keyword list in `po/CMakeLists.txt` is load-bearing: xgettext's defaults recognise none of `_`,
`N_`, `C_`, `Q_`, `g_dgettext` or `g_dngettext`, and anything missing from it is absent from the
template without a warning. For the same reason a msgid must be one literal — a macro spliced into
it (`"%" G_GINT64_FORMAT " dropout"`) extracts as bare `"%"`, silently.

`src/host/` is exempt from the no-GTK rule only for `<libintl.h>`: `ts_i18n.hpp` gives it `TS_()`
bound explicitly to the domain, so its dialog messages and the song-information prose translate
without the layer gaining glib. Its names are deliberately not `_`/`N_`, which would collide with
`<glib/gi18n.h>` in any file including both.

Tone and drum-kit names, module designations and SysEx message names are never translated; they are
the module's own display text and have to match the patch charts.

### Adding an engine setting

gschema key → `TSEngineSettings` in `host/ts_types.h` (+ `TSEngineSettingsDefault`) →
`Session::options()` → `apply()` in `app/ts-settings.cpp` → a row in `ts-prefs-dialog.cpp`.
Values that cost no rebuild (looping, latency, MIDI auto-connect) are applied beside the struct via
`g_object_set`, not through it — latency in particular must not rebuild, since the ring is sized
once for `max_latency_ms` and this only moves the fill target inside it.

Then `cmake --build --preset dev --target update-po`: the key's `<summary>`/`<description>` and the
row's title and subtitle are four new strings, and nothing rescans the sources on its own.

## Conventions

Comments here explain *why*, in full prose, and are dense — non-obvious decisions carry a paragraph
naming the alternative that was rejected and the concrete failure it caused. Match that when
editing; a change that removes a constraint should remove the comment that guards it, and a new
constraint deserves the same treatment. Warnings are `-Wall -Wextra -Wpedantic -Wshadow
-Wdouble-promotion` deliberately *without* `-Wold-style-cast` (GTK macros are C casts); vendored and
generated sources are compiled with `-w` rather than being fixed.

## Packaging

`packaging/flatpak/` is the primary distribution — libadwaita 1.6 is unavailable on Ubuntu 24.04 LTS
and Debian 12, so no native `.deb` is possible. Its source is `type: dir` on the working tree, not a
git ref, because a release is built from a tag where no branch name resolves. `packaging/arch/` has
a PKGBUILD linking the system libadwaita.

CI (`.github/workflows/build.yml`) builds the bundle on every push; `main` becomes a prerelease
tagged `main-<sha>` (ten newest kept, tagged versions never pruned), a `v*` tag becomes a release.

`NOTICE.md` is not optional in any package: the engine's reverb and chorus coefficients are
Roland-derived and NativeTS requires the notice to travel with any binary. The PKGBUILD asserts it.

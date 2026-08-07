#pragma once

#include "host/ts_types.h"

#include <adwaita.h>

/// Everything the interface knows about the engine.
///
/// The counterpart of the Apple front end's observable `Player`, and it works the same way: the
/// render thread republishes a snapshot about fifty times a second, and this polls it ten times a
/// second and turns what changed into `notify` signals. Polling rather than pushing is deliberate --
/// it keeps every engine read on one thread and off the audio path, and a display has no use for
/// more than ten updates a second anyway.
///
/// Nothing else in the interface touches `ts::host::Player`. Widgets bind to these properties.
#define TS_TYPE_PLAYER_MODEL (ts_player_model_get_type())
G_DECLARE_FINAL_TYPE(TsPlayerModel, ts_player_model, TS, PLAYER_MODEL, GObject)

TsPlayerModel* ts_player_model_new(void);

// -- Loading -------------------------------------------------------------------------------------

/// Opens a `SCCore.dll`, starts the output device and begins ticking.
///
/// `verify_fully` hashes all 27 MB, which is what a newly imported file needs; a file already
/// verified once needs only the size and timestamp check. Blocks for as long as that takes, so a
/// full verification belongs on a worker thread.
gboolean ts_player_model_load_rom(TsPlayerModel* self, const char* path, gboolean verify_fully,
                                  GError** error);

gboolean ts_player_model_load_song(TsPlayerModel* self, const char* path, GError** error);
void ts_player_model_unload_song(TsPlayerModel* self);

// -- Transport -----------------------------------------------------------------------------------

void ts_player_model_play(TsPlayerModel* self);
void ts_player_model_pause(TsPlayerModel* self);
void ts_player_model_toggle_playing(TsPlayerModel* self);
void ts_player_model_seek(TsPlayerModel* self, double seconds);
void ts_player_model_panic(TsPlayerModel* self);

// -- Mixer ---------------------------------------------------------------------------------------

/// A `GListModel` of `TsPart`, sixty-four long and stable for the model's lifetime.
///
/// Which of them the song addresses is the `present` property, and it changes only when a song is
/// loaded or unloaded. Because the list itself never changes length, a filter over it has no reason
/// to re-run on its own -- watch the `presence-changed` signal and invalidate the filter there.
GListModel* ts_player_model_get_parts(TsPlayerModel* self);

void ts_player_model_set_muted(TsPlayerModel* self, int part, gboolean muted);
void ts_player_model_set_soloed(TsPlayerModel* self, int part, gboolean soloed);
void ts_player_model_reset_channels(TsPlayerModel* self);

// -- Engine settings -----------------------------------------------------------------------------

/// The whole settings struct at once, so one change costs one rebuild rather than one per field.
TSEngineSettings ts_player_model_get_settings(TsPlayerModel* self);
void ts_player_model_apply_settings(TsPlayerModel* self, TSEngineSettings settings);

/// Live MIDI in, which is independent of whether a song is loaded.
void ts_player_model_set_midi_auto_connect(TsPlayerModel* self, gboolean enabled);

// -- Export --------------------------------------------------------------------------------------

/// Renders the loaded song to a WAV on a worker thread, over a second generator, while playback
/// continues. Progress arrives on the `export-progress` property; cancelling makes the render abort.
void ts_player_model_export_wav(TsPlayerModel* self, const char* path, GCancellable* cancellable,
                                GAsyncReadyCallback callback, gpointer user_data);

gboolean ts_player_model_export_wav_finish(TsPlayerModel* self, GAsyncResult* result,
                                           GError** error);

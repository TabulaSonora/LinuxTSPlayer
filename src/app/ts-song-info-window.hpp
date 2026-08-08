#pragma once

#include "app/ts-player-model.hpp"

#include <adwaita.h>

/// What the loaded file says about itself: its tracks, its text, its lyric sheet, its loop, and the
/// module it asks to be played on.
///
/// A window rather than a dialog, and the difference is the point: it is meant to be left open
/// beside the player while the music runs, and it follows whatever file is loaded rather than being
/// a snapshot of the one that was loaded when it opened.
#define TS_TYPE_SONG_INFO_WINDOW (ts_song_info_window_get_type())
G_DECLARE_FINAL_TYPE(TsSongInfoWindow, ts_song_info_window, TS, SONG_INFO_WINDOW, AdwWindow)

GtkWindow* ts_song_info_window_new(TsPlayerModel* model, GtkWindow* parent);

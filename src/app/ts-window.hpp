#pragma once

#include <adwaita.h>

#define TS_TYPE_WINDOW (ts_window_get_type())
G_DECLARE_FINAL_TYPE(TsWindow, ts_window, TS, WINDOW, AdwApplicationWindow)

TsWindow* ts_window_new(AdwApplication* application);

/// Loads a song, reporting failure the way the transport does. Used by the open dialog, the drop
/// target, the recents menu and `GApplication::open` alike.
void ts_window_open_file(TsWindow* self, GFile* file);

/// Opens the engine preferences over this window.
void ts_window_present_preferences(TsWindow* self);

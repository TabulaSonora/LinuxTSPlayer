#pragma once

#include "app/ts-player-model.hpp"

#include <adwaita.h>

/// Voice, effects, output and latency.
///
/// Every row writes a GSettings key rather than touching the engine, which is what makes one
/// changed control cost exactly one generator rebuild and makes the setting persist for free.
#define TS_TYPE_PREFS_DIALOG (ts_prefs_dialog_get_type())
G_DECLARE_FINAL_TYPE(TsPrefsDialog, ts_prefs_dialog, TS, PREFS_DIALOG, AdwPreferencesDialog)

AdwDialog* ts_prefs_dialog_new(TsPlayerModel* model);

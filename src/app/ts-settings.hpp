#pragma once

#include "app/ts-player-model.hpp"

#include <adwaita.h>

/// The application's GSettings, opened once.
GSettings* ts_settings_get(void);

/// Makes the stored settings drive the engine.
///
/// GSettings is the source of truth rather than a mirror of it: the preferences dialog writes keys
/// and this applies them, so there is one place a value lives and persistence costs nothing extra.
/// Applying rewrites the whole `TSEngineSettings` at once, so one changed key is one generator
/// rebuild -- not one per field.
///
/// Applies the current values immediately, then on every change.
void ts_settings_bind_model(GSettings* settings, TsPlayerModel* model);

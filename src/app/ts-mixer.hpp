#pragma once

#include "app/ts-player-model.hpp"

#include <adwaita.h>

/// The scrolling list of mixer strips.
///
/// Shows only the parts the loaded song addresses. A four-port file has sixty-four slots and
/// typically uses a third of them, so listing all of them would bury the ones that matter.
#define TS_TYPE_MIXER (ts_mixer_get_type())
G_DECLARE_FINAL_TYPE(TsMixer, ts_mixer, TS, MIXER, GtkWidget)

GtkWidget* ts_mixer_new(TsPlayerModel* model);

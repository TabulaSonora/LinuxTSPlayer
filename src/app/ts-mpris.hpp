#pragma once

#include "app/ts-player-model.hpp"

#include <adwaita.h>

/// MPRIS2, so the shell's media controls, the lock screen and headset keys reach the transport.
///
/// The Linux counterpart of the Apple front end's now-playing centre, and it takes the same lesson
/// from it: the elapsed position is published with a rate and clients extrapolate from there, so
/// properties are announced when the state actually changes and never on the display tick.
#define TS_TYPE_MPRIS (ts_mpris_get_type())
G_DECLARE_FINAL_TYPE(TsMpris, ts_mpris, TS, MPRIS, GObject)

/// Claims the bus name and starts publishing. Owns nothing but a reference to the model; the window
/// is used only to answer `Raise`.
TsMpris* ts_mpris_new(TsPlayerModel* model, GtkWindow* window);

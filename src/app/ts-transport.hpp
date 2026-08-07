#pragma once

#include "app/ts-player-model.hpp"

#include <adwaita.h>

/// Song name, scrubber, transport buttons and the engine's status line.
#define TS_TYPE_TRANSPORT (ts_transport_get_type())
G_DECLARE_FINAL_TYPE(TsTransport, ts_transport, TS, TRANSPORT, GtkWidget)

GtkWidget* ts_transport_new(TsPlayerModel* model);

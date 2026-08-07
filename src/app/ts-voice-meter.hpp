#pragma once

#include <adwaita.h>

/// The little row of bars showing how many voices a part is sounding.
///
/// Drawn rather than built out of child widgets: there is one of these per mixer strip and up to
/// sixty-four strips, so six boxes each would be several hundred widgets to lay out every time the
/// count changes ten times a second.
#define TS_TYPE_VOICE_METER (ts_voice_meter_get_type())
G_DECLARE_FINAL_TYPE(TsVoiceMeter, ts_voice_meter, TS, VOICE_METER, GtkWidget)

GtkWidget* ts_voice_meter_new(void);

void ts_voice_meter_set_voices(TsVoiceMeter* self, int voices);
int ts_voice_meter_get_voices(TsVoiceMeter* self);

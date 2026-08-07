#pragma once

#include "app/ts-part-object.hpp"
#include "app/ts-player-model.hpp"

#include <adwaita.h>

/// One mixer strip.
///
/// Created once per visible row by the list view's factory and then rebound as the list scrolls, so
/// it holds no state of its own beyond which part it is currently showing.
#define TS_TYPE_PART_ROW (ts_part_row_get_type())
G_DECLARE_FINAL_TYPE(TsPartRow, ts_part_row, TS, PART_ROW, GtkWidget)

GtkWidget* ts_part_row_new(TsPlayerModel* model);

/// Points the row at a part, or at nothing when the list view recycles it.
void ts_part_row_bind(TsPartRow* self, TsPart* part);
void ts_part_row_unbind(TsPartRow* self);

#pragma once

#include "host/ts_session.hpp"

#include <adwaita.h>

/// One mixer strip's worth of state, as a GObject so a GtkListView can bind to it.
///
/// There are always sixty-four of these and they are created once; the mixer filters them down to
/// the parts the loaded song addresses. Updating in place rather than rebuilding the store is what
/// keeps rows from churning ten times a second.
#define TS_TYPE_PART (ts_part_get_type())
G_DECLARE_FINAL_TYPE(TsPart, ts_part, TS, PART, GObject)

TsPart* ts_part_new(int index);

/// `port * 16 + channel`, which is how the engine addresses parts and how mute and solo are keyed.
int ts_part_get_index(TsPart* self);

gboolean ts_part_get_present(TsPart* self);
gboolean ts_part_get_muted(TsPart* self);
gboolean ts_part_get_soloed(TsPart* self);
int ts_part_get_voices(TsPart* self);

/// Copies a snapshot's part state in, emitting `notify` only for what actually changed.
///
/// `dimmed` is not the part's own state: it means something *else* is soloed, which the mixer knows
/// and a single part does not.
void ts_part_update(TsPart* self, const ts::host::PartState& state, gboolean dimmed);

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

/// The port this part's slot belongs to, counted from zero.
int ts_part_get_port(TsPart* self);

/// The channel the part *listens on*, counted from zero -- not the slot it occupies.
///
/// Parts are matched by receive channel rather than indexed by it, so a strip has to be labelled
/// and ordered by this. The two agree until a bulk dump or a `40 1x 02` moves a part, and then a
/// mixer built on the slot number is describing something the file is not addressing.
int ts_part_get_channel(TsPart* self);

/// What the part is actually playing, in the numbers a patch list is indexed by: the program and
/// the bank select pair. The sounding instrument's *name* answers "what is this"; this answers
/// "which entry is it", which is the question anyone comparing against a module's chart has.
const char* ts_part_get_detail(TsPart* self);

/// "Port A, channel 1" spelled out, because the strip only has room for "A1".
const char* ts_part_get_address(TsPart* self);

gboolean ts_part_get_present(TsPart* self);
gboolean ts_part_get_muted(TsPart* self);
gboolean ts_part_get_soloed(TsPart* self);
int ts_part_get_voices(TsPart* self);

/// CC#7, CC#10 and CC#11 as the engine holds them right now, 0..127.
///
/// These are the part's own controllers rather than anything this program keeps: a strip showing
/// them is reading the same registers the file writes, which is why a fader over one of them can
/// only ever be a window onto it. Expression has no fader because nothing in the interface should
/// invite writing it -- it is here so a volume readout can say what else is scaling the part.
int ts_part_get_volume(TsPart* self);
int ts_part_get_pan(TsPart* self);
int ts_part_get_expression(TsPart* self);

/// Copies a snapshot's part state in, emitting `notify` only for what actually changed.
///
/// `dimmed` is not the part's own state: it means something *else* is soloed, which the mixer knows
/// and a single part does not.
void ts_part_update(TsPart* self, const ts::host::PartState& state, gboolean dimmed);

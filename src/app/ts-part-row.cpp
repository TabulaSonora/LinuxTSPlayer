#include "app/ts-part-row.hpp"

#include "app/ts-voice-meter.hpp"

#include "tabulasonora/sequence.hpp"

#include <glib/gi18n.h>

#include <cstdlib>
#include <string>

namespace {

/// The strip width below which the chips are dropped rather than squeezed; see
/// `ts_part_row_size_allocate` for why the decision is made there and not by the box.
///
/// Chosen from what is left over: the address, the meter and the two toggles take about 190px of a
/// strip, so at this width the name and the chips have some 230px between them -- enough for the
/// longest tone name in the map beside "Drums · SC-8820" without either being cut.
constexpr int tags_minimum_width = 420;

/// How wide a fader is allowed to be. Not a floor like the transport's gain slider: the faders are
/// the one part of a strip that must not grow, because everything to their right has to line up down
/// a column of sixteen and the name to their left is what should be taking the spare width.
constexpr int fader_width = 60;

/// The strip width below which the two faders are dropped, decided in the same place and for the
/// same reason as `tags_minimum_width`.
///
/// The pair costs `2 * fader_width` plus the two 12px box gaps around them, so this is the chip
/// threshold plus that: at this width the name and the chips have back exactly the room they had at
/// 420px without any faders, and neither has to give any of it up.
constexpr int faders_minimum_width = tags_minimum_width + 2 * fader_width + 24;

/// How long a fader goes on ignoring the engine after sending a value, waiting to hear it back.
///
/// Real time rather than a count of syncs, because a sync is not a clock: `ts_part_row_sync` runs
/// once per changed property, so a tick that moves the name and the voice count as well as the
/// volume runs it three times and a countdown would expire three times as fast.
constexpr gint64 fader_echo_us = 500 * 1000;

/// One of the two controller faders and the state that keeps it from fighting its own engine.
struct Fader {
    GtkWidget* scale;
    GtkAdjustment* adjustment;

    /// The last value this row sent, or -1 when it is simply following the engine.
    int pending;

    /// When to stop waiting for `pending` to come back.
    gint64 deadline;
};

/// Whether the engine's value for a fader may be written into it.
///
/// A fader that just followed the snapshot would fight the pointer: the model republishes ten times a
/// second and any of those ticks can land mid-drag carrying the value from before the drag began, so
/// the slider would be dragged forward by the hand and snapped back by the clock. A fader that has
/// just sent something therefore stops taking the engine's word until it hears its own value back.
///
/// The wait is bounded rather than left to resolve itself, and the bound is doing real work in two
/// cases. A part with its GS volume receive switch off never echoes anything, and a song that sends
/// its own CC7 while a value of ours is outstanding echoes something else -- in both the fader has to
/// give up and show what is true, which is exactly what the timeout makes it do.
bool fader_accepts(Fader& fader, int value)
{
    if (fader.pending < 0) {
        return true;
    }

    if (value == fader.pending || g_get_monotonic_time() >= fader.deadline) {
        fader.pending = -1;
        return true;
    }

    return false;
}

/// Pan as the module spells it: 1..127 read as L63 through R63, with 64 in the middle.
///
/// Zero is not a position. The engine folds a CC#10 of 0 to 1 because the controller cannot reach the
/// random-pan setting -- only the GS panpot SysEx can write that -- so the fader does not offer it
/// and a part that has it is described in words instead.
std::string pan_text(int pan)
{
    if (pan <= 0) {
        return _("Pan random, set by SysEx");
    }

    const int offset = pan - 64;
    if (offset == 0) {
        return _("Pan centre");
    }

    // Two whole format strings rather than a letter concatenated onto a word: L and R are English
    // initials, and a language that writes them differently -- or puts the distance before the
    // side -- has nowhere to say so if the string is assembled here.
    /* TRANSLATORS: %d is the distance left of centre, 1-63. L stands for left. */
    g_autofree char* text = offset < 0 ? g_strdup_printf(_("Pan L%d"), -offset)
                                       /* TRANSLATORS: %d is the distance right of centre, 1-63. */
                                       : g_strdup_printf(_("Pan R%d"), offset);
    return text;
}

} // namespace

struct _TsPartRow {
    GtkWidget parent_instance;

    /// Not a reference: the model outlives every row, and an owning cycle through the window would
    /// keep the engine alive after the window closed.
    TsPlayerModel* model;

    TsPart* part;

    GtkWidget* label;
    GtkWidget* names;
    GtkWidget* name;
    GtkWidget* tags;
    Fader volume;
    Fader pan;

    GtkWidget* meter;
    GtkWidget* mute;
    GtkWidget* solo;

    /// Whether the bound part has any chips at all. Kept apart from whether they are *shown*, which
    /// is a question of how wide the strip has been allocated and is answered in
    /// `ts_part_row_apply_width`.
    gboolean has_tags;

    /// Set while the row is writing its own toggles from the bound part, so the handlers below can
    /// tell a rebind from a click and not send the engine a change it just reported.
    gboolean updating;

    gulong notify_id;
};

G_DEFINE_FINAL_TYPE(TsPartRow, ts_part_row, GTK_TYPE_WIDGET)

// -- What fits ------------------------------------------------------------------------------------

/// Drops the faders, and then the chips, on a strip too narrow to carry them beside the name.
///
/// The box cannot be asked to do either, with any combination of children expanding: given less than
/// its children want it fills the smaller natural size first and takes the whole shortfall out of the
/// larger, which at 380px left "Syn.Str…" beside an untouched "SC-8820" -- the wrong one of the two
/// kept whole, since the chip only qualifies a name the reader can no longer read. Hiding a child
/// outright gives the name every pixel there is, and the bank numbers the chip is derived from are in
/// the tooltip regardless.
///
/// The faders go one threshold earlier than the chips, so the order under a shrinking window is
/// faders, then chips. They are first because they are the only thing here that a narrow window
/// cannot render *usefully* -- a 30px trough is not something anyone sets a level with, where a
/// squeezed name is still a name -- and because what they show can be asked for again by widening the
/// window, where a truncated name is simply gone.
///
/// Deciding on the strip's own width is what keeps this from oscillating, and the thresholds are far
/// enough above the row's own minimum for that to hold: at any width a 360px window can produce, the
/// faders are already hidden, so hiding them can never be what makes the room that would bring them
/// back.
///
/// Two mechanisms that look right are not. `notify::width` never fires, because `GtkWidget` has no
/// `width` property to notify -- only `width-request` -- and a `notify::` detail that matches no
/// property is silently never emitted rather than warned about. And a `size_allocate` override on a
/// widget that sets a layout manager type never runs either: allocation is dispatched to the manager
/// instead of the class vfunc. This row carried such an override for exactly that reason and it had
/// never once been called; the chips appeared to work only because the sync below happens to
/// re-apply the same rule on every part update. That is why the row measures and allocates its own
/// child now rather than borrowing `GtkBinLayout` -- see `ts_part_row_size_allocate`.
static void ts_part_row_apply_width(TsPartRow* self, int width)
{
    const gboolean faders = width >= faders_minimum_width;
    gtk_widget_set_visible(self->volume.scale, faders);
    gtk_widget_set_visible(self->pan.scale, faders);

    gtk_widget_set_visible(self->tags, self->has_tags && width >= tags_minimum_width);
}

// -- Binding -------------------------------------------------------------------------------------

static void ts_part_row_sync(TsPartRow* self)
{
    if (self->part == nullptr) {
        return;
    }

    self->updating = TRUE;

    g_autofree char* label = nullptr;
    g_autofree char* name = nullptr;
    g_autofree char* tags = nullptr;
    int voices = 0;
    int volume = 0;
    int pan = 0;
    gboolean muted = FALSE;
    gboolean soloed = FALSE;
    gboolean dimmed = FALSE;

    g_object_get(self->part, "label", &label, "name", &name, "tags", &tags, "voices", &voices,
                 "volume", &volume, "pan", &pan, "muted", &muted, "soloed", &soloed, "dimmed",
                 &dimmed, nullptr);

    gtk_label_set_text(GTK_LABEL(self->label), label != nullptr ? label : "");
    gtk_label_set_text(GTK_LABEL(self->name), name != nullptr ? name : "");
    gtk_label_set_text(GTK_LABEL(self->tags), tags != nullptr ? tags : "");
    // Re-applied here as well as on resize, because whether a part *has* chips changes with the part
    // and not with the width.
    self->has_tags = tags != nullptr && *tags != '\0';
    ts_part_row_apply_width(self, gtk_widget_get_width(GTK_WIDGET(self)));

    // Tooltips are answered on demand rather than written here; see the query handler below.

    ts_voice_meter_set_voices(TS_VOICE_METER(self->meter), voices);

    // The faders follow the engine unless they are waiting to hear back from it; see `fader_accepts`.
    // The pan adjustment starts at 1, so a part left on the random-pan setting shows hard left here
    // and says so in its tooltip -- there is no position on a CC#10 fader that means "random".
    if (fader_accepts(self->volume, volume)) {
        gtk_adjustment_set_value(self->volume.adjustment, volume);
    }
    if (fader_accepts(self->pan, pan)) {
        gtk_adjustment_set_value(self->pan.adjustment, pan);
    }

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->mute), muted);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->solo), soloed);

    // Dimming says "something else is soloed", which is a property of the mixer rather than of this
    // strip -- so it is the one thing here the part cannot tell us on its own.
    gtk_widget_set_opacity(GTK_WIDGET(self), dimmed ? 0.45 : 1.0);

    self->updating = FALSE;
}

/// The strip has room for a name and an abbreviation; the numbers behind both go in tooltips, read
/// from the bound part when GTK asks for them.
///
/// Asked for rather than pushed: setting a tooltip re-triggers a display-wide tooltip query, so a
/// row that rewrote its tooltips as the part changed would cancel the hover timer of whatever the
/// pointer was actually resting on.
static gboolean on_query_tooltip(GtkWidget* widget, int x, int y, gboolean keyboard,
                                 GtkTooltip* tooltip, gpointer user_data)
{
    (void)x;
    (void)y;
    (void)keyboard;

    auto* self = TS_PART_ROW(user_data);
    if (self->part == nullptr) {
        return FALSE;
    }

    // The faders carry no visible number -- a strip has no room for one and a column of sixteen
    // readouts would be unreadable anyway -- so this is where their value is actually spelled out.
    if (widget == self->volume.scale) {
        // Expression comes along because CC#7 alone does not say how loud a part is: the two are
        // multiplied, and a score that leaves volume alone and rides expression would otherwise look
        // as though nothing were moving.
        g_autofree char* text =
            /* TRANSLATORS: the two CC values that together decide how loud a part is. */
            g_strdup_printf(_("Volume %d · Expression %d"), ts_part_get_volume(self->part),
                            ts_part_get_expression(self->part));
        gtk_tooltip_set_text(tooltip, text);
        return TRUE;
    }

    if (widget == self->pan.scale) {
        gtk_tooltip_set_text(tooltip, pan_text(ts_part_get_pan(self->part)).c_str());
        return TRUE;
    }

    const char* text =
        widget == self->label ? ts_part_get_address(self->part) : ts_part_get_detail(self->part);
    if (text == nullptr || *text == '\0') {
        return FALSE;
    }

    gtk_tooltip_set_text(tooltip, text);
    return TRUE;
}

static void on_part_notify(GObject* part, GParamSpec* spec, gpointer user_data)
{
    (void)part;
    (void)spec;
    ts_part_row_sync(TS_PART_ROW(user_data));
}

void ts_part_row_bind(TsPartRow* self, TsPart* part)
{
    ts_part_row_unbind(self);

    self->part = TS_PART(g_object_ref(part));
    self->notify_id = g_signal_connect(part, "notify", G_CALLBACK(on_part_notify), self);

    // Rows are recycled by the list view, so a fader can arrive here still waiting to hear a value
    // back from the part it was showing a moment ago. Carried over, that wait would suppress the
    // first sync of the *new* part and leave the strip showing the old one's level.
    self->volume.pending = -1;
    self->pan.pending = -1;

    ts_part_row_sync(self);
}

void ts_part_row_unbind(TsPartRow* self)
{
    if (self->part == nullptr) {
        return;
    }

    g_clear_signal_handler(&self->notify_id, self->part);
    g_clear_object(&self->part);
}

// -- Faders --------------------------------------------------------------------------------------

/// Sends what a fader now reads as a control change on the part's *receive* channel.
///
/// The receive channel and not the slot: the engine dispatches a controller by walking the parts
/// looking for one that listens on it, so a message addressed to the slot would go to whatever part
/// happens to be on that channel -- which after a bulk dump is not the strip under the pointer. The
/// same reason means one fader can move two parts, when a file has pointed both at one channel; that
/// is the module's own behaviour and the other strip will show it within a tick.
static void ts_part_row_send(TsPartRow* self, Fader& fader, int controller)
{
    if (self->updating || self->part == nullptr) {
        return;
    }

    const int value = static_cast<int>(gtk_adjustment_get_value(fader.adjustment));

    fader.pending = value;
    fader.deadline = g_get_monotonic_time() + fader_echo_us;

    ts_player_model_send_control(self->model, ts_part_get_port(self->part),
                                 ts_part_get_channel(self->part), controller, value);
}

static void on_volume_changed(GtkAdjustment* adjustment, gpointer user_data)
{
    (void)adjustment;
    auto* self = TS_PART_ROW(user_data);
    ts_part_row_send(self, self->volume, 7);
}

static void on_pan_changed(GtkAdjustment* adjustment, gpointer user_data)
{
    (void)adjustment;
    auto* self = TS_PART_ROW(user_data);
    ts_part_row_send(self, self->pan, 10);
}

// -- Toggles -------------------------------------------------------------------------------------

static void on_mute_toggled(GtkToggleButton* button, gpointer user_data)
{
    auto* self = TS_PART_ROW(user_data);
    if (self->updating || self->part == nullptr) {
        return;
    }
    ts_player_model_set_muted(self->model, ts_part_get_index(self->part),
                              gtk_toggle_button_get_active(button));
}

static void on_solo_toggled(GtkToggleButton* button, gpointer user_data)
{
    auto* self = TS_PART_ROW(user_data);
    if (self->updating || self->part == nullptr) {
        return;
    }
    ts_player_model_set_soloed(self->model, ts_part_get_index(self->part),
                               gtk_toggle_button_get_active(button));
}

// -- Construction --------------------------------------------------------------------------------

static void ts_part_row_dispose(GObject* object)
{
    auto* self = TS_PART_ROW(object);

    ts_part_row_unbind(self);

    if (GtkWidget* child = gtk_widget_get_first_child(GTK_WIDGET(self))) {
        gtk_widget_unparent(child);
    }

    G_OBJECT_CLASS(ts_part_row_parent_class)->dispose(object);
}

/// What `GtkBinLayout` would do, which is all this row needs of a layout: hand the single child
/// everything.
///
/// Done by hand because a layout manager is what stops `ts_part_row_size_allocate` below from being
/// called at all, and that is the only place the strip can be told how wide it has ended up.
///
/// The empty case is real and is the reason both of these check: dispose unparents the box, and a row
/// can still be measured after that.
static void ts_part_row_measure(GtkWidget* widget, GtkOrientation orientation, int for_size,
                                int* minimum, int* natural, int* minimum_baseline,
                                int* natural_baseline)
{
    GtkWidget* child = gtk_widget_get_first_child(widget);
    if (child == nullptr) {
        *minimum = 0;
        *natural = 0;
        *minimum_baseline = -1;
        *natural_baseline = -1;
        return;
    }

    gtk_widget_measure(child, orientation, for_size, minimum, natural, minimum_baseline,
                       natural_baseline);
}

/// Applies the width rules and then gives the child the whole allocation.
///
/// Changing a child's visibility here queues a resize from inside an allocation, which settles on the
/// next pass rather than looping: the thresholds are a pure function of a width that comes from the
/// list view, so the second pass reaches the same answer and stops. libadwaita's own breakpoint bin
/// applies its setters from the same place for the same reason.
static void ts_part_row_size_allocate(GtkWidget* widget, int width, int height, int baseline)
{
    ts_part_row_apply_width(TS_PART_ROW(widget), width);

    if (GtkWidget* child = gtk_widget_get_first_child(widget)) {
        gtk_widget_allocate(child, width, height, baseline, nullptr);
    }
}

static void ts_part_row_class_init(TsPartRowClass* klass)
{
    G_OBJECT_CLASS(klass)->dispose = ts_part_row_dispose;

    GTK_WIDGET_CLASS(klass)->measure = ts_part_row_measure;
    GTK_WIDGET_CLASS(klass)->size_allocate = ts_part_row_size_allocate;

    gtk_widget_class_set_css_name(GTK_WIDGET_CLASS(klass), "partrow");
}

/// Builds one controller fader and appends it.
///
/// Deliberately not `hexpand`: these are the fixed part of the strip and the name box stays the only
/// thing that grows, so widening the window lengthens the *name* rather than the troughs and the
/// chips keep an edge to line up on down the column. `draw_value` is off for the same reason the
/// strip has no other numbers on it -- sixteen readouts is not something anyone reads -- and the
/// value is spelled out in the tooltip instead.
static void ts_part_row_build_fader(TsPartRow* self, Fader& fader, GtkWidget* box, double lower,
                                    double value, const char* label, GCallback changed)
{
    fader.pending = -1;
    fader.adjustment = gtk_adjustment_new(value, lower, 127, 1, 8, 0);
    fader.scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, fader.adjustment);

    gtk_scale_set_draw_value(GTK_SCALE(fader.scale), FALSE);

    // Whole controller values only: a scale left to its own resolution emits a change per pixel of
    // travel, and every one of those would be a MIDI message the engine has to dispatch.
    gtk_range_set_round_digits(GTK_RANGE(fader.scale), 0);

    gtk_widget_set_size_request(fader.scale, fader_width, -1);
    gtk_widget_set_valign(fader.scale, GTK_ALIGN_CENTER);

    // Nothing on a strip is labelled on screen, so the name has to be handed to the accessibility
    // tree directly; there is no caption for it to be inferred from.
    gtk_accessible_update_property(GTK_ACCESSIBLE(fader.scale), GTK_ACCESSIBLE_PROPERTY_LABEL, label,
                                   -1);

    gtk_widget_set_has_tooltip(fader.scale, TRUE);
    g_signal_connect(fader.scale, "query-tooltip", G_CALLBACK(on_query_tooltip), self);
    g_signal_connect(fader.adjustment, "value-changed", changed, self);

    gtk_box_append(GTK_BOX(box), fader.scale);
}

static void ts_part_row_init(TsPartRow* self)
{
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);

    // Four pixels, and the sidebar row's own vertical padding zeroed in the stylesheet, because all
    // sixteen parts of a score have to be visible at once -- see the note there. This is the only
    // vertical spacing a strip has left, so it is deliberately small rather than merely reduced.
    gtk_widget_set_margin_top(box, 4);
    gtk_widget_set_margin_bottom(box, 4);

    // A1..D16, at a fixed width so the instrument names below each other line up rather than
    // stepping in and out as the channel number reaches two digits.
    self->label = gtk_label_new("");
    gtk_widget_add_css_class(self->label, "caption");
    gtk_widget_add_css_class(self->label, "numeric");
    gtk_widget_add_css_class(self->label, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(self->label), 0.0F);
    gtk_widget_set_size_request(self->label, 34, -1);
    gtk_widget_set_valign(self->label, GTK_ALIGN_CENTER);
    gtk_widget_set_has_tooltip(self->label, TRUE);
    g_signal_connect(self->label, "query-tooltip", G_CALLBACK(on_query_tooltip), self);
    gtk_box_append(GTK_BOX(box), self->label);

    // Name and chips beside each other, not stacked. Stacked they cost two lines of text where the
    // rest of the strip needs one, and two lines is most of the difference between sixteen strips
    // fitting on a 1080p screen and twelve of them fitting; read across, the chips still sit next to
    // the name they qualify. Baseline-aligned rather than centred, so the caption sits on the same
    // line as the name instead of floating against a taller glyph.
    self->names = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* names = self->names;
    gtk_widget_set_valign(names, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(names, TRUE);
    gtk_widget_set_has_tooltip(names, TRUE);
    g_signal_connect(names, "query-tooltip", G_CALLBACK(on_query_tooltip), self);

    self->name = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(self->name), 0.0F);
    gtk_label_set_ellipsize(GTK_LABEL(self->name), PANGO_ELLIPSIZE_END);
    gtk_label_set_single_line_mode(GTK_LABEL(self->name), TRUE);
    gtk_widget_set_valign(self->name, GTK_ALIGN_BASELINE_CENTER);
    gtk_box_append(GTK_BOX(names), self->name);

    // The chips expand and align right, which puts them against the meter rather than trailing the
    // name: down a column of sixteen strips that gives them an edge to line up on, where following
    // the name would leave them scattered across the width. What happens when there is not room for
    // both is decided in the allocation override above, not here -- a box would take the shortfall
    // out of the name instead.
    self->tags = gtk_label_new("");
    gtk_widget_add_css_class(self->tags, "caption");
    gtk_widget_add_css_class(self->tags, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(self->tags), 1.0F);
    gtk_widget_set_hexpand(self->tags, TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(self->tags), PANGO_ELLIPSIZE_END);
    gtk_label_set_single_line_mode(GTK_LABEL(self->tags), TRUE);
    gtk_widget_set_valign(self->tags, GTK_ALIGN_BASELINE_CENTER);
    gtk_box_append(GTK_BOX(names), self->tags);

    gtk_box_append(GTK_BOX(box), names);

    // The two controllers that decide how a part sits in the mix, and the only writable thing on a
    // strip besides the toggles. They are windows onto CC#7 and CC#10 rather than a gain of this
    // program's own: moving one sends the controller the file would have sent, so the file's next one
    // overrides it and a seek -- which replays controllers -- puts every strip back where the score
    // says. A host-side trim riding on top would survive both, and would then be describing a mix
    // that neither the module nor an export of it would ever produce.
    ts_part_row_build_fader(self, self->volume, box, 0, ts::sequence_builder::default_volume,
                            _("Volume"), G_CALLBACK(on_volume_changed));

    // Pan starts at 1, not 0: the engine folds a CC#10 of zero up to one because the controller
    // cannot express the random-pan setting, and a fader whose bottom end silently became something
    // else would be lying about its own travel.
    ts_part_row_build_fader(self, self->pan, box, 1, ts::sequence_builder::default_pan, _("Pan"),
                            G_CALLBACK(on_pan_changed));

    // No fill from the left, because pan has no low end to fill from -- what a reader wants from it
    // is the distance either side of centre. The centre itself is drawn in the trough by the
    // stylesheet rather than by `gtk_scale_add_mark`, which would add a marks row twelve pixels tall
    // to a strip that is thirty-two pixels tall in total.
    gtk_scale_set_has_origin(GTK_SCALE(self->pan.scale), FALSE);
    gtk_widget_add_css_class(self->pan.scale, "pan");

    self->meter = ts_voice_meter_new();
    gtk_box_append(GTK_BOX(box), self->meter);

    // Muting happens at the mix, not at the note: the part goes on consuming polyphony, which is
    // what the module does and what makes a muted part still steal voices from the others.
    /* TRANSLATORS: one letter, the mute button on a mixer strip. There is room for one glyph
       only; pick whatever initial reads as "mute" in your language. */
    self->mute = gtk_toggle_button_new_with_label(C_("mixer strip button", "M"));
    gtk_widget_add_css_class(self->mute, "flat");
    gtk_widget_set_valign(self->mute, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(
        self->mute, _("Silence this part at the mix. It goes on using polyphony either way."));
    g_signal_connect(self->mute, "toggled", G_CALLBACK(on_mute_toggled), self);
    gtk_box_append(GTK_BOX(box), self->mute);

    /* TRANSLATORS: one letter, the solo button on a mixer strip. As with the mute button, there
       is room for one glyph only. */
    self->solo = gtk_toggle_button_new_with_label(C_("mixer strip button", "S"));
    gtk_widget_add_css_class(self->solo, "flat");
    gtk_widget_set_valign(self->solo, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(self->solo, _("Hear only the soloed parts."));
    g_signal_connect(self->solo, "toggled", G_CALLBACK(on_solo_toggled), self);
    gtk_box_append(GTK_BOX(box), self->solo);

    gtk_widget_set_parent(box, GTK_WIDGET(self));
}

GtkWidget* ts_part_row_new(TsPlayerModel* model)
{
    auto* self = TS_PART_ROW(g_object_new(TS_TYPE_PART_ROW, nullptr));
    self->model = model;
    return GTK_WIDGET(self);
}

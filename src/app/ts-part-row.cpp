#include "app/ts-part-row.hpp"

#include "app/ts-voice-meter.hpp"

namespace {

/// The strip width below which the chips are dropped rather than squeezed; see
/// `ts_part_row_size_allocate` for why the decision is made there and not by the box.
///
/// Chosen from what is left over: the address, the meter and the two toggles take about 190px of a
/// strip, so at this width the name and the chips have some 230px between them -- enough for the
/// longest tone name in the map beside "Drums · SC-8820" without either being cut.
constexpr int tags_minimum_width = 420;

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
    GtkWidget* meter;
    GtkWidget* mute;
    GtkWidget* solo;

    /// Whether the bound part has any chips at all. Kept apart from whether they are *shown*, which
    /// is a question of how wide the strip has been allocated and is answered during allocation.
    gboolean has_tags;

    /// Set while the row is writing its own toggles from the bound part, so the handlers below can
    /// tell a rebind from a click and not send the engine a change it just reported.
    gboolean updating;

    gulong notify_id;
};

G_DEFINE_FINAL_TYPE(TsPartRow, ts_part_row, GTK_TYPE_WIDGET)

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
    gboolean muted = FALSE;
    gboolean soloed = FALSE;
    gboolean dimmed = FALSE;

    g_object_get(self->part, "label", &label, "name", &name, "tags", &tags, "voices", &voices,
                 "muted", &muted, "soloed", &soloed, "dimmed", &dimmed, nullptr);

    gtk_label_set_text(GTK_LABEL(self->label), label != nullptr ? label : "");
    gtk_label_set_text(GTK_LABEL(self->name), name != nullptr ? name : "");
    gtk_label_set_text(GTK_LABEL(self->tags), tags != nullptr ? tags : "");
    self->has_tags = tags != nullptr && *tags != '\0';
    gtk_widget_set_visible(self->tags, self->has_tags && gtk_widget_get_width(GTK_WIDGET(self)) >=
                                                             tags_minimum_width);

    // Tooltips are answered on demand rather than written here; see the query handler below.

    ts_voice_meter_set_voices(TS_VOICE_METER(self->meter), voices);

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

/// Drops the chips on a strip too narrow to carry them beside the name.
///
/// The box cannot be asked to do this, with either child expanding or neither: given less than both
/// labels want it fills the smaller natural size first and takes the whole shortfall out of the
/// larger, which at 380px left "Syn.Str…" beside an untouched "SC-8820" -- the wrong one of the two
/// kept whole, since the chip only qualifies a name the reader can no longer read. Hiding the chip
/// outright gives the name every pixel there is, and the bank numbers the chip is derived from are
/// in the tooltip regardless.
///
/// Deciding on the strip's own width is what keeps this from oscillating: the width comes from the
/// list view, not from what the strip contains, so hiding the chips never creates the room that
/// would bring them back.
static void ts_part_row_size_allocate(GtkWidget* widget, int width, int height, int baseline)
{
    auto* self = TS_PART_ROW(widget);

    gtk_widget_set_visible(self->tags, self->has_tags && width >= tags_minimum_width);

    GTK_WIDGET_CLASS(ts_part_row_parent_class)->size_allocate(widget, width, height, baseline);
}

static void ts_part_row_class_init(TsPartRowClass* klass)
{
    G_OBJECT_CLASS(klass)->dispose = ts_part_row_dispose;

    GTK_WIDGET_CLASS(klass)->size_allocate = ts_part_row_size_allocate;

    gtk_widget_class_set_layout_manager_type(GTK_WIDGET_CLASS(klass), GTK_TYPE_BIN_LAYOUT);
    gtk_widget_class_set_css_name(GTK_WIDGET_CLASS(klass), "partrow");
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

    self->meter = ts_voice_meter_new();
    gtk_box_append(GTK_BOX(box), self->meter);

    // Muting happens at the mix, not at the note: the part goes on consuming polyphony, which is
    // what the module does and what makes a muted part still steal voices from the others.
    self->mute = gtk_toggle_button_new_with_label("M");
    gtk_widget_add_css_class(self->mute, "flat");
    gtk_widget_set_valign(self->mute, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(
        self->mute, "Silence this part at the mix. It goes on using polyphony either way.");
    g_signal_connect(self->mute, "toggled", G_CALLBACK(on_mute_toggled), self);
    gtk_box_append(GTK_BOX(box), self->mute);

    self->solo = gtk_toggle_button_new_with_label("S");
    gtk_widget_add_css_class(self->solo, "flat");
    gtk_widget_set_valign(self->solo, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(self->solo, "Hear only the soloed parts.");
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

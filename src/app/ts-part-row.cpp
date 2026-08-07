#include "app/ts-part-row.hpp"

#include "app/ts-voice-meter.hpp"

struct _TsPartRow {
    GtkWidget parent_instance;

    /// Not a reference: the model outlives every row, and an owning cycle through the window would
    /// keep the engine alive after the window closed.
    TsPlayerModel* model;

    TsPart* part;

    GtkWidget* label;
    GtkWidget* name;
    GtkWidget* tags;
    GtkWidget* meter;
    GtkWidget* mute;
    GtkWidget* solo;

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
    gtk_widget_set_visible(self->tags, tags != nullptr && *tags != '\0');

    ts_voice_meter_set_voices(TS_VOICE_METER(self->meter), voices);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->mute), muted);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->solo), soloed);

    // Dimming says "something else is soloed", which is a property of the mixer rather than of this
    // strip -- so it is the one thing here the part cannot tell us on its own.
    gtk_widget_set_opacity(GTK_WIDGET(self), dimmed ? 0.45 : 1.0);

    self->updating = FALSE;
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

static void ts_part_row_class_init(TsPartRowClass* klass)
{
    G_OBJECT_CLASS(klass)->dispose = ts_part_row_dispose;

    gtk_widget_class_set_layout_manager_type(GTK_WIDGET_CLASS(klass), GTK_TYPE_BIN_LAYOUT);
    gtk_widget_class_set_css_name(GTK_WIDGET_CLASS(klass), "partrow");
}

static void ts_part_row_init(TsPartRow* self)
{
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);

    // A1..D16, at a fixed width so the instrument names below each other line up rather than
    // stepping in and out as the channel number reaches two digits.
    self->label = gtk_label_new("");
    gtk_widget_add_css_class(self->label, "caption");
    gtk_widget_add_css_class(self->label, "numeric");
    gtk_widget_add_css_class(self->label, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(self->label), 0.0F);
    gtk_widget_set_size_request(self->label, 34, -1);
    gtk_widget_set_valign(self->label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), self->label);

    GtkWidget* names = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_valign(names, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(names, TRUE);

    self->name = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(self->name), 0.0F);
    gtk_label_set_ellipsize(GTK_LABEL(self->name), PANGO_ELLIPSIZE_END);
    gtk_label_set_single_line_mode(GTK_LABEL(self->name), TRUE);
    gtk_box_append(GTK_BOX(names), self->name);

    self->tags = gtk_label_new("");
    gtk_widget_add_css_class(self->tags, "caption");
    gtk_widget_add_css_class(self->tags, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(self->tags), 0.0F);
    gtk_label_set_ellipsize(GTK_LABEL(self->tags), PANGO_ELLIPSIZE_END);
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

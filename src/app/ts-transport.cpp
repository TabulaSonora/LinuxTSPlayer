#include "app/ts-transport.hpp"

#include <cmath>
#include <initializer_list>

struct _TsTransport {
    GtkWidget parent_instance;

    TsPlayerModel* model;

    GtkWidget* title;
    GtkWidget* subtitle;

    GtkWidget* scale;
    GtkAdjustment* adjustment;
    GtkWidget* elapsed;
    GtkWidget* total;

    GtkWidget* play;
    GtkWidget* play_icon;
    GtkWidget* loop;

    GtkWidget* voices;
    GtkWidget* xg;
    GtkWidget* dropouts;

    /// Shown only while an export is running. A row of its own rather than a button in the
    /// transport: file operations belong in the menu, and a permanently-present "Export WAV…"
    /// button was single-handedly setting the window's minimum width.
    GtkWidget* export_revealer;
    GtkWidget* export_progress;

    /// True from the moment the scrubber is grabbed until it is let go.
    ///
    /// While it is set the engine's reported position is ignored, because otherwise every tick
    /// would yank the handle out from under the pointer.
    gboolean scrubbing;

    /// Set while the widget writes its own controls from the model, so the handlers can tell that
    /// from a click.
    gboolean updating;
};

G_DEFINE_FINAL_TYPE(TsTransport, ts_transport, GTK_TYPE_WIDGET)

namespace {

/// m:ss, which is what the Apple build shows and what fits a two-digit-minute song without
/// re-laying-out the row.
char* format_time(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0) {
        seconds = 0.0;
    }
    const auto total = static_cast<int>(seconds);
    return g_strdup_printf("%d:%02d", total / 60, total % 60);
}

} // namespace

// -- Model -> widgets ----------------------------------------------------------------------------

static void ts_transport_sync(TsTransport* self)
{
    self->updating = TRUE;

    g_autofree char* song = nullptr;
    g_autofree char* rom = nullptr;
    double position = 0.0;
    double duration = 0.0;
    gboolean playing = FALSE;
    gboolean looping = FALSE;
    gboolean xg = FALSE;
    int voices = 0;
    int capacity = 0;
    gint64 underruns = 0;

    g_object_get(self->model, "song-name", &song, "rom-name", &rom, "position", &position,
                 "duration", &duration, "playing", &playing, "looping", &looping, "xg-mode", &xg,
                 "active-voices", &voices, "voice-capacity", &capacity, "underruns", &underruns,
                 nullptr);

    const gboolean has_song = song != nullptr;

    gtk_label_set_text(GTK_LABEL(self->title), has_song ? song : "No file open");

    if (rom != nullptr) {
        g_autofree char* subtitle = g_strdup_printf("Sound Canvas voice · %s", rom);
        gtk_label_set_text(GTK_LABEL(self->subtitle), subtitle);
    } else {
        gtk_label_set_text(GTK_LABEL(self->subtitle), "");
    }

    // A zero-length range makes GtkScale misbehave, so an empty transport gets a nominal one.
    gtk_adjustment_set_upper(self->adjustment, duration > 0.0 ? duration : 1.0);
    if (!self->scrubbing) {
        gtk_adjustment_set_value(self->adjustment, position);
    }
    gtk_widget_set_sensitive(self->scale, has_song);

    g_autofree char* elapsed =
        format_time(self->scrubbing ? gtk_adjustment_get_value(self->adjustment) : position);
    g_autofree char* total = format_time(duration);
    gtk_label_set_text(GTK_LABEL(self->elapsed), elapsed);
    gtk_label_set_text(GTK_LABEL(self->total), total);

    gtk_button_set_icon_name(GTK_BUTTON(self->play),
                             playing ? "media-playback-pause-symbolic"
                                     : "media-playback-start-symbolic");
    gtk_widget_set_tooltip_text(self->play, playing ? "Pause" : "Play");
    gtk_widget_set_sensitive(self->play, has_song);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->loop), looping);

    g_autofree char* count = g_strdup_printf("%d/%d voices", voices, capacity);
    gtk_label_set_text(GTK_LABEL(self->voices), count);

    gtk_widget_set_visible(self->xg, xg);

    gboolean exporting = FALSE;
    double export_progress = 0.0;
    g_object_get(self->model, "exporting", &exporting, "export-progress", &export_progress,
                 nullptr);

    gtk_revealer_set_reveal_child(GTK_REVEALER(self->export_revealer), exporting);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(self->export_progress), export_progress);

    gtk_widget_set_visible(self->dropouts, underruns > 0);
    if (underruns > 0) {
        g_autofree char* text = g_strdup_printf(
            g_dngettext(nullptr, "%" G_GINT64_FORMAT " dropout", "%" G_GINT64_FORMAT " dropouts",
                        static_cast<gulong>(underruns)),
            underruns);
        gtk_label_set_text(GTK_LABEL(self->dropouts), text);
    }

    self->updating = FALSE;
}

static void on_model_notify(GObject* model, GParamSpec* spec, gpointer user_data)
{
    (void)model;
    (void)spec;
    ts_transport_sync(TS_TRANSPORT(user_data));
}

// -- Widgets -> model ----------------------------------------------------------------------------

static void on_scrub_begin(GtkGestureDrag* gesture, double x, double y, gpointer user_data)
{
    (void)gesture;
    (void)x;
    (void)y;
    TS_TRANSPORT(user_data)->scrubbing = TRUE;
}

static void on_scrub_end(GtkGestureDrag* gesture, double x, double y, gpointer user_data)
{
    (void)gesture;
    (void)x;
    (void)y;

    auto* self = TS_TRANSPORT(user_data);
    self->scrubbing = FALSE;
    ts_player_model_seek(self->model, gtk_adjustment_get_value(self->adjustment));
}

static void on_scale_changed(GtkAdjustment* adjustment, gpointer user_data)
{
    auto* self = TS_TRANSPORT(user_data);
    if (!self->scrubbing) {
        return;
    }
    // Keep the elapsed readout under the pointer while dragging, without seeking until release --
    // seeking on every motion event would have the engine replaying controllers continuously.
    g_autofree char* elapsed = format_time(gtk_adjustment_get_value(adjustment));
    gtk_label_set_text(GTK_LABEL(self->elapsed), elapsed);
}

static void on_loop_toggled(GtkToggleButton* button, gpointer user_data)
{
    auto* self = TS_TRANSPORT(user_data);
    if (self->updating) {
        return;
    }
    g_object_set(self->model, "looping", gtk_toggle_button_get_active(button), nullptr);
}

// -- Construction --------------------------------------------------------------------------------

static void ts_transport_dispose(GObject* object)
{
    if (GtkWidget* child = gtk_widget_get_first_child(GTK_WIDGET(object))) {
        gtk_widget_unparent(child);
    }
    G_OBJECT_CLASS(ts_transport_parent_class)->dispose(object);
}

static void ts_transport_class_init(TsTransportClass* klass)
{
    G_OBJECT_CLASS(klass)->dispose = ts_transport_dispose;
    gtk_widget_class_set_layout_manager_type(GTK_WIDGET_CLASS(klass), GTK_TYPE_BIN_LAYOUT);
    gtk_widget_class_set_css_name(GTK_WIDGET_CLASS(klass), "transport");
}

namespace {

GtkWidget* icon_button(const char* icon, const char* tooltip, const char* action)
{
    GtkWidget* button = gtk_button_new_from_icon_name(icon);
    gtk_widget_add_css_class(button, "flat");
    gtk_widget_set_tooltip_text(button, tooltip);
    gtk_actionable_set_action_name(GTK_ACTIONABLE(button), action);
    return button;
}

} // namespace

static void ts_transport_init(TsTransport* self)
{
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 18);

    // -- Heading --
    GtkWidget* heading = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

    self->title = gtk_label_new("No file open");
    gtk_widget_add_css_class(self->title, "heading");
    gtk_label_set_xalign(GTK_LABEL(self->title), 0.0F);
    gtk_label_set_ellipsize(GTK_LABEL(self->title), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_append(GTK_BOX(heading), self->title);

    self->subtitle = gtk_label_new("");
    gtk_widget_add_css_class(self->subtitle, "caption");
    gtk_widget_add_css_class(self->subtitle, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(self->subtitle), 0.0F);
    gtk_label_set_ellipsize(GTK_LABEL(self->subtitle), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_append(GTK_BOX(heading), self->subtitle);

    gtk_box_append(GTK_BOX(box), heading);

    // -- Scrubber --
    GtkWidget* seek = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);

    self->adjustment = gtk_adjustment_new(0, 0, 1, 1, 10, 0);
    self->scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, self->adjustment);
    gtk_scale_set_draw_value(GTK_SCALE(self->scale), FALSE);
    gtk_widget_set_hexpand(self->scale, TRUE);
    gtk_box_append(GTK_BOX(seek), self->scale);

    GtkWidget* times = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    self->elapsed = gtk_label_new("0:00");
    self->total = gtk_label_new("0:00");
    for (GtkWidget* label : {self->elapsed, self->total}) {
        gtk_widget_add_css_class(label, "caption");
        gtk_widget_add_css_class(label, "numeric");
        gtk_widget_add_css_class(label, "dim-label");
    }
    gtk_widget_set_hexpand(self->elapsed, TRUE);
    gtk_label_set_xalign(GTK_LABEL(self->elapsed), 0.0F);
    gtk_label_set_xalign(GTK_LABEL(self->total), 1.0F);
    gtk_box_append(GTK_BOX(times), self->elapsed);
    gtk_box_append(GTK_BOX(times), self->total);
    gtk_box_append(GTK_BOX(seek), times);

    gtk_box_append(GTK_BOX(box), seek);

    // -- Controls --
    GtkWidget* controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    self->play = gtk_button_new_from_icon_name("media-playback-start-symbolic");
    gtk_widget_add_css_class(self->play, "suggested-action");
    gtk_widget_add_css_class(self->play, "circular");
    gtk_widget_set_tooltip_text(self->play, "Play");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(self->play), "win.play-pause");
    gtk_box_append(GTK_BOX(controls), self->play);

    gtk_box_append(GTK_BOX(controls),
                   icon_button("media-skip-backward-symbolic", "Go back to the start", "win.rewind"));

    self->loop = gtk_toggle_button_new();
    gtk_button_set_icon_name(GTK_BUTTON(self->loop), "media-playlist-repeat-symbolic");
    gtk_widget_add_css_class(self->loop, "flat");
    gtk_widget_set_tooltip_text(
        self->loop,
        "Repeat at the file's own loop points, or over the whole file if it declares none");
    g_signal_connect(self->loop, "toggled", G_CALLBACK(on_loop_toggled), self);
    gtk_box_append(GTK_BOX(controls), self->loop);

    gtk_box_append(GTK_BOX(controls),
                   icon_button("dialog-warning-symbolic",
                               "Panic: silence every voice and return each part to its power-on "
                               "state",
                               "win.panic"));

    GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(controls), spacer);

    gtk_box_append(GTK_BOX(box), controls);

    // -- Export progress, only while there is an export --
    //
    // A revealer rather than a button that lives here permanently. Export is a file operation and
    // belongs in the menu with the rest of them, where it already is with a keyboard shortcut; a
    // button sized to the words "Export WAV…" was on its own setting the window's minimum width and
    // keeping it from ever reaching the narrow sizes the interface claims to support.
    GtkWidget* exporting = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    GtkWidget* exporting_label = gtk_label_new("Exporting");
    gtk_widget_add_css_class(exporting_label, "caption");
    gtk_widget_add_css_class(exporting_label, "dim-label");
    gtk_label_set_ellipsize(GTK_LABEL(exporting_label), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(exporting), exporting_label);

    self->export_progress = gtk_progress_bar_new();
    gtk_widget_set_hexpand(self->export_progress, TRUE);
    gtk_widget_set_valign(self->export_progress, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(exporting), self->export_progress);

    GtkWidget* cancel = gtk_button_new_from_icon_name("process-stop-symbolic");
    gtk_widget_add_css_class(cancel, "flat");
    gtk_widget_set_tooltip_text(cancel, "Stop exporting");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(cancel), "win.cancel-export");
    gtk_box_append(GTK_BOX(exporting), cancel);

    self->export_revealer = gtk_revealer_new();
    gtk_revealer_set_child(GTK_REVEALER(self->export_revealer), exporting);
    gtk_box_append(GTK_BOX(box), self->export_revealer);

    // -- Status --
    GtkWidget* status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

    self->voices = gtk_label_new("0/0 voices");
    gtk_widget_add_css_class(self->voices, "caption");
    gtk_widget_add_css_class(self->voices, "dim-label");
    gtk_widget_add_css_class(self->voices, "numeric");
    gtk_box_append(GTK_BOX(status), self->voices);

    self->xg = gtk_label_new("XG");
    gtk_widget_add_css_class(self->xg, "caption-heading");
    gtk_widget_add_css_class(self->xg, "accent");
    gtk_widget_set_visible(self->xg, FALSE);
    gtk_widget_set_tooltip_text(self->xg, "The file put the engine into XG mode");
    gtk_box_append(GTK_BOX(status), self->xg);

    GtkWidget* status_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(status_spacer, TRUE);
    gtk_box_append(GTK_BOX(status), status_spacer);

    self->dropouts = gtk_label_new("");
    gtk_widget_add_css_class(self->dropouts, "caption");
    gtk_widget_add_css_class(self->dropouts, "warning");
    gtk_widget_set_visible(self->dropouts, FALSE);
    gtk_widget_set_tooltip_text(self->dropouts,
                                "The engine could not keep the buffer fed. Raise the buffer in "
                                "Preferences.");
    gtk_box_append(GTK_BOX(status), self->dropouts);

    gtk_box_append(GTK_BOX(box), status);

    gtk_widget_set_parent(box, GTK_WIDGET(self));
}

GtkWidget* ts_transport_new(TsPlayerModel* model)
{
    auto* self = TS_TRANSPORT(g_object_new(TS_TYPE_TRANSPORT, nullptr));
    self->model = model;

    // A drag gesture on the scale, purely to know when the handle is held. GtkRange has no
    // "editing finished" signal the way an NSSlider does, so this stands in for it.
    GtkGesture* drag = gtk_gesture_drag_new();
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(drag), GTK_PHASE_CAPTURE);
    g_signal_connect(drag, "drag-begin", G_CALLBACK(on_scrub_begin), self);
    g_signal_connect(drag, "drag-end", G_CALLBACK(on_scrub_end), self);
    gtk_widget_add_controller(self->scale, GTK_EVENT_CONTROLLER(drag));

    g_signal_connect(self->adjustment, "value-changed", G_CALLBACK(on_scale_changed), self);

    g_signal_connect_object(model, "notify", G_CALLBACK(on_model_notify), self, G_CONNECT_DEFAULT);
    ts_transport_sync(self);

    return GTK_WIDGET(self);
}

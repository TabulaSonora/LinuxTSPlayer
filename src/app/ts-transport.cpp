#include "app/ts-transport.hpp"

#include "app/ts-settings.hpp"

#include <glib/gi18n.h>

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

    /// Output gain. Bound to its GSettings key rather than to the model, so it is not synced from
    /// `ts_transport_sync` like everything else here.
    GtkWidget* gain;
    GtkAdjustment* gain_adjustment;
    GtkWidget* gain_readout;

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

    gtk_label_set_text(GTK_LABEL(self->title), has_song ? song : _("No file open"));

    if (rom != nullptr) {
        /* TRANSLATORS: %s is the module the file is being played on, e.g. "SC-8820". */
        g_autofree char* subtitle = g_strdup_printf(_("Sound Canvas voice · %s"), rom);
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
    gtk_widget_set_sensitive(self->play, has_song);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->loop), looping);

    /* TRANSLATORS: sounding voices over the polyphony ceiling, e.g. "12/64 voices". Deliberately
       one form rather than a plural: the noun agrees with the ceiling, not with the first number,
       so "1/64 voice" would be wrong. */
    g_autofree char* count = g_strdup_printf(_("%d/%d voices"), voices, capacity);
    gtk_label_set_text(GTK_LABEL(self->voices), count);

    gtk_widget_set_visible(self->xg, xg);

    gboolean exporting = FALSE;
    double export_progress = 0.0;
    g_object_get(self->model, "exporting", &exporting, "export-progress", &export_progress,
                 nullptr);

    gtk_revealer_set_reveal_child(GTK_REVEALER(self->export_revealer), exporting);
    if (exporting) {
        // Pulsed from the same ten-times-a-second tick that drives everything else here.
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(self->export_progress));
    }
    (void)export_progress;

    gtk_widget_set_visible(self->dropouts, underruns > 0);
    if (underruns > 0) {
        // %ld rather than G_GINT64_FORMAT; see the same construction in ts-prefs-dialog.cpp for why
        // a macro inside the msgid defeats extraction.
        g_autofree char* text = g_strdup_printf(
            g_dngettext(nullptr, "%ld dropout", "%ld dropouts", static_cast<gulong>(underruns)),
            static_cast<long>(underruns));
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

/// The gain readout, which follows the slider rather than the engine.
///
/// Shown as a percentage although the setting is a linear multiplier, because "120%" says what
/// moving the handle did and "1.20" only says what it is called. The engine's own unity is 100%.
static void on_gain_changed(GtkAdjustment* adjustment, gpointer user_data)
{
    auto* self = TS_TRANSPORT(user_data);
    const double gain = gtk_adjustment_get_value(adjustment);
    /* TRANSLATORS: a percentage of the engine's own output level. Some locales put a space
       before the sign. */
    g_autofree char* text = g_strdup_printf(_("%d%%"), static_cast<int>(gain * 100.0 + 0.5));
    gtk_label_set_text(GTK_LABEL(self->gain_readout), text);
}

static void on_loop_toggled(GtkToggleButton* button, gpointer user_data)
{
    auto* self = TS_TRANSPORT(user_data);
    if (self->updating) {
        return;
    }
    g_object_set(self->model, "looping", gtk_toggle_button_get_active(button), nullptr);
}

/// Answered on demand: the label flips with the transport, and pushing it would re-trigger a
/// display-wide tooltip query each time playback started or stopped.
static gboolean on_play_query_tooltip(GtkWidget*, int, int, gboolean, GtkTooltip* tooltip,
                                      gpointer user_data)
{
    auto* self = TS_TRANSPORT(user_data);
    gboolean playing = FALSE;
    g_object_get(self->model, "playing", &playing, nullptr);
    gtk_tooltip_set_text(tooltip, playing ? _("Pause") : _("Play"));
    return TRUE;
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

    self->title = gtk_label_new(_("No file open"));
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
    gtk_widget_set_tooltip_text(self->scale,
                                _("Seek. Jumping replays the controllers up to that point, so "
                                  "the parts sound as they would have."));
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
    gtk_widget_set_has_tooltip(self->play, TRUE);
    g_signal_connect(self->play, "query-tooltip", G_CALLBACK(on_play_query_tooltip), self);
    gtk_widget_add_css_class(self->play, "suggested-action");
    gtk_widget_add_css_class(self->play, "circular");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(self->play), "win.play-pause");
    gtk_box_append(GTK_BOX(controls), self->play);

    gtk_box_append(GTK_BOX(controls),
                   icon_button("media-skip-backward-symbolic", _("Go back to the start"),
                               "win.rewind"));

    self->loop = gtk_toggle_button_new();
    gtk_button_set_icon_name(GTK_BUTTON(self->loop), "media-playlist-repeat-symbolic");
    gtk_widget_add_css_class(self->loop, "flat");
    gtk_widget_set_tooltip_text(
        self->loop,
        _("Repeat at the file's own loop points, or over the whole file if it declares none"));
    g_signal_connect(self->loop, "toggled", G_CALLBACK(on_loop_toggled), self);
    gtk_box_append(GTK_BOX(controls), self->loop);

    gtk_box_append(GTK_BOX(controls),
                   icon_button("dialog-warning-symbolic",
                               _("Panic: silence every voice and return each part to its "
                                 "power-on state"),
                               "win.panic"));

    GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(controls), spacer);

    // -- Gain --
    //
    // Beside the transport buttons rather than in Preferences, where it started: it is the one
    // engine value that is played rather than configured. Everything else in that dialog changes
    // what the synth *is* and costs a generator rebuild; gain changes how loud the mix comes out,
    // is applied live by `Session::set_settings`, and is the thing a listener reaches for while a
    // file is playing. Reaching for it through a menu and a dialog for that is a mismatch.
    GtkWidget* gain_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_tooltip_text(gain_box,
                                _("Gain on the finished mix. 100% is the engine's own level; "
                                  "above it a loud file can clip."));

    GtkWidget* gain_icon = gtk_image_new_from_icon_name("audio-volume-high-symbolic");
    gtk_widget_add_css_class(gain_icon, "dim-label");
    gtk_box_append(GTK_BOX(gain_box), gain_icon);

    self->gain_adjustment = gtk_adjustment_new(1.0, 0.0, 2.0, 0.05, 0.25, 0);
    self->gain = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, self->gain_adjustment);
    gtk_scale_set_draw_value(GTK_SCALE(self->gain), FALSE);
    // 72px is a floor, not the width: the slider takes half of whatever the row has spare.
    //
    // A single number cannot serve both ends of this layout. The row is four buttons wide already,
    // and 360px -- the minimum the window advertises, and the width the narrow layout exists to
    // serve -- leaves barely 80px beside them, so anything wider as a *minimum* pushes the
    // transport past what the window says it can be and the adaptive layout stops working. At the
    // 700px default there is room to spare and no reason to leave a stub.
    //
    // So the slider expands, and so does the spacer in front of it, which makes GtkBox split the
    // slack between them evenly. The slider grows with the window and the gap ahead of it grows at
    // the same rate, so the group stays pinned to the right instead of drifting into the middle of
    // the row. Below that the floor takes over: at 360px there is no slack, the spacer collapses,
    // and the slider is the 72px the row can afford.
    //
    // An AdwClamp was the obvious way to also cap the growth, and does not work here: it measures
    // its child's natural size, not its maximum, so a box with slack to give hands it the floor and
    // the slider never grows at all. Capping it properly would take a widget that reports a natural
    // size larger than its minimum, which is not worth a subclass for a slider.
    gtk_widget_set_size_request(self->gain, 72, -1);
    gtk_widget_set_hexpand(self->gain, TRUE);
    gtk_widget_set_valign(self->gain, GTK_ALIGN_CENTER);

    // A tick at unity, unlabelled. The scale spans a range whose interesting point is not an end,
    // and without the tick there is no way to put the handle back on the engine's own level by eye.
    gtk_scale_add_mark(GTK_SCALE(self->gain), 1.0, GTK_POS_BOTTOM, nullptr);

    // Two decimals, which is what the key stores and what the removed spin row offered. It also
    // bounds the writes a drag makes: without it every motion event within one pixel would be a
    // fresh value, and the binding below turns each value into a settings write.
    gtk_range_set_round_digits(GTK_RANGE(self->gain), 2);
    gtk_box_append(GTK_BOX(gain_box), self->gain);

    // Built from the same format string the handler uses, so unity cannot be punctuated one way
    // here and another way the moment the slider moves.
    g_autofree char* unity_gain = g_strdup_printf(_("%d%%"), 100);
    self->gain_readout = gtk_label_new(unity_gain);
    gtk_widget_add_css_class(self->gain_readout, "caption");
    gtk_widget_add_css_class(self->gain_readout, "numeric");
    gtk_widget_add_css_class(self->gain_readout, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(self->gain_readout), 1.0F);
    gtk_label_set_width_chars(GTK_LABEL(self->gain_readout), 4);
    gtk_box_append(GTK_BOX(gain_box), self->gain_readout);

    gtk_box_append(GTK_BOX(controls), gain_box);

    gtk_box_append(GTK_BOX(box), controls);

    // -- Export progress, only while there is an export --
    //
    // A revealer rather than a button that lives here permanently. Export is a file operation and
    // belongs in the menu with the rest of them, where it already is with a keyboard shortcut; a
    // button sized to the words "Export WAV…" was on its own setting the window's minimum width and
    // keeping it from ever reaching the narrow sizes the interface claims to support.
    GtkWidget* exporting = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    GtkWidget* exporting_label = gtk_label_new(_("Exporting…"));
    gtk_widget_add_css_class(exporting_label, "caption");
    gtk_widget_add_css_class(exporting_label, "dim-label");
    gtk_label_set_ellipsize(GTK_LABEL(exporting_label), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(exporting), exporting_label);

    // Indeterminate, and no cancel button beside it, because neither would be truthful. An export
    // is one render call -- that is what makes its output match the reference -- so there is no
    // fraction to report from inside it and no point at which it can be interrupted. A bar that sat
    // at zero and jumped to full, or a Cancel that did nothing, would both be worse than saying
    // plainly that something is in progress.
    self->export_progress = gtk_progress_bar_new();
    gtk_widget_set_hexpand(self->export_progress, TRUE);
    gtk_widget_set_valign(self->export_progress, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(exporting), self->export_progress);

    self->export_revealer = gtk_revealer_new();
    gtk_revealer_set_child(GTK_REVEALER(self->export_revealer), exporting);
    gtk_box_append(GTK_BOX(box), self->export_revealer);

    // -- Status --
    GtkWidget* status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

    // Through the same format string as the live readout, so the placeholder cannot end up
    // worded differently from what replaces it a tenth of a second later.
    g_autofree char* no_voices = g_strdup_printf(_("%d/%d voices"), 0, 0);
    self->voices = gtk_label_new(no_voices);
    gtk_widget_add_css_class(self->voices, "caption");
    gtk_widget_add_css_class(self->voices, "dim-label");
    gtk_widget_add_css_class(self->voices, "numeric");
    gtk_box_append(GTK_BOX(status), self->voices);

    self->xg = gtk_label_new("XG");
    gtk_widget_add_css_class(self->xg, "caption-heading");
    gtk_widget_add_css_class(self->xg, "accent");
    gtk_widget_set_visible(self->xg, FALSE);
    gtk_widget_set_tooltip_text(self->xg, _("The file put the engine into XG mode"));
    gtk_box_append(GTK_BOX(status), self->xg);

    GtkWidget* status_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(status_spacer, TRUE);
    gtk_box_append(GTK_BOX(status), status_spacer);

    self->dropouts = gtk_label_new("");
    gtk_widget_add_css_class(self->dropouts, "caption");
    gtk_widget_add_css_class(self->dropouts, "warning");
    gtk_widget_set_visible(self->dropouts, FALSE);
    gtk_widget_set_tooltip_text(self->dropouts,
                                _("The engine could not keep the buffer fed. Raise the buffer "
                                  "in Preferences."));
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

    // Straight to the key, not through the model, and in both directions.
    //
    // GSettings is the source of truth here as everywhere else, so the slider writes the key and
    // `ts_settings_bind_model` applies it -- one path from the value to the engine, whether it was
    // moved here, set over MPRIS, or restored at startup. Binding it also means the handle follows
    // an MPRIS volume change for free, which a one-way control would not.
    g_signal_connect(self->gain_adjustment, "value-changed", G_CALLBACK(on_gain_changed), self);
    g_settings_bind(ts_settings_get(), "output-gain", self->gain_adjustment, "value",
                    G_SETTINGS_BIND_DEFAULT);

    g_signal_connect_object(model, "notify", G_CALLBACK(on_model_notify), self, G_CONNECT_DEFAULT);
    ts_transport_sync(self);

    return GTK_WIDGET(self);
}

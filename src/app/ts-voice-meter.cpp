#include "app/ts-voice-meter.hpp"

#include <algorithm>

namespace {

/// Six bars, as in the Apple build. It is not a count so much as a sense of how busy a part is;
/// past six the exact number stops being readable at a glance and the tooltip carries it instead.
constexpr int bar_count = 6;
constexpr int bar_width = 3;
constexpr int bar_height = 12;
constexpr int bar_gap = 2;

constexpr float empty_alpha = 0.18F;

} // namespace

struct _TsVoiceMeter {
    GtkWidget parent_instance;
    int voices;
};

G_DEFINE_FINAL_TYPE(TsVoiceMeter, ts_voice_meter, GTK_TYPE_WIDGET)

enum { PROP_VOICES = 1, N_PROPS };
static GParamSpec* properties[N_PROPS];

static void ts_voice_meter_measure(GtkWidget* widget, GtkOrientation orientation, int for_size,
                                   int* minimum, int* natural, int* minimum_baseline,
                                   int* natural_baseline)
{
    (void)widget;
    (void)for_size;

    const int size = orientation == GTK_ORIENTATION_HORIZONTAL
                         ? bar_count * bar_width + (bar_count - 1) * bar_gap
                         : bar_height;

    *minimum = size;
    *natural = size;
    *minimum_baseline = -1;
    *natural_baseline = -1;
}

static void ts_voice_meter_snapshot(GtkWidget* widget, GtkSnapshot* snapshot)
{
    auto* self = TS_VOICE_METER(widget);

    const int height = gtk_widget_get_height(widget);
    const float top = static_cast<float>(std::max(0, (height - bar_height) / 2));

    GdkRGBA foreground;
    gtk_widget_get_color(widget, &foreground);

    GdkRGBA* accent = adw_style_manager_get_accent_color_rgba(
        adw_style_manager_get_for_display(gtk_widget_get_display(widget)));

    GdkRGBA dim = foreground;
    dim.alpha *= empty_alpha;

    for (int i = 0; i < bar_count; ++i) {
        const graphene_rect_t bounds = GRAPHENE_RECT_INIT(
            static_cast<float>(i * (bar_width + bar_gap)), top,
            static_cast<float>(bar_width), static_cast<float>(bar_height));

        GskRoundedRect rounded;
        gsk_rounded_rect_init_from_rect(&rounded, &bounds, static_cast<float>(bar_width) / 2.0F);

        gtk_snapshot_push_rounded_clip(snapshot, &rounded);
        gtk_snapshot_append_color(snapshot, i < self->voices ? accent : &dim, &bounds);
        gtk_snapshot_pop(snapshot);
    }

    gdk_rgba_free(accent);
}

static void ts_voice_meter_get_property(GObject* object, guint id, GValue* value, GParamSpec* spec)
{
    auto* self = TS_VOICE_METER(object);

    if (id == PROP_VOICES) {
        g_value_set_int(value, self->voices);
    } else {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, id, spec);
    }
}

static void ts_voice_meter_set_property(GObject* object, guint id, const GValue* value,
                                        GParamSpec* spec)
{
    if (id == PROP_VOICES) {
        ts_voice_meter_set_voices(TS_VOICE_METER(object), g_value_get_int(value));
    } else {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, id, spec);
    }
}

static void ts_voice_meter_class_init(TsVoiceMeterClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass* widget_class = GTK_WIDGET_CLASS(klass);

    object_class->get_property = ts_voice_meter_get_property;
    object_class->set_property = ts_voice_meter_set_property;

    widget_class->measure = ts_voice_meter_measure;
    widget_class->snapshot = ts_voice_meter_snapshot;

    properties[PROP_VOICES] = g_param_spec_int(
        "voices", nullptr, nullptr, 0, G_MAXINT, 0,
        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_properties(object_class, N_PROPS, properties);

    gtk_widget_class_set_css_name(widget_class, "voicemeter");
}

/// Answers when GTK asks, rather than being pushed a new string every time the count moves.
///
/// This is the difference between a tooltip that works during playback and one that never appears.
/// `gtk_widget_set_tooltip_text` re-triggers a tooltip query, and a query acts on the whole display
/// -- so a meter rewriting its tooltip several times a second was cancelling the hover timer of
/// every other widget in the window, and while a song played nothing anywhere had time to show.
static gboolean ts_voice_meter_query_tooltip(GtkWidget* widget, int x, int y, gboolean keyboard,
                                             GtkTooltip* tooltip, gpointer user_data)
{
    (void)x;
    (void)y;
    (void)keyboard;
    (void)user_data;

    auto* self = TS_VOICE_METER(widget);
    if (self->voices == 0) {
        return FALSE;
    }

    g_autofree char* text = g_strdup_printf(
        g_dngettext(nullptr, "%d voice sounding", "%d voices sounding", self->voices), self->voices);
    gtk_tooltip_set_text(tooltip, text);
    return TRUE;
}

static void ts_voice_meter_init(TsVoiceMeter* self)
{
    gtk_widget_set_valign(GTK_WIDGET(self), GTK_ALIGN_CENTER);

    gtk_widget_set_has_tooltip(GTK_WIDGET(self), TRUE);
    g_signal_connect(self, "query-tooltip", G_CALLBACK(ts_voice_meter_query_tooltip), nullptr);
}

GtkWidget* ts_voice_meter_new(void)
{
    return GTK_WIDGET(g_object_new(TS_TYPE_VOICE_METER, nullptr));
}

void ts_voice_meter_set_voices(TsVoiceMeter* self, int voices)
{
    const int wanted = std::max(0, voices);
    if (self->voices == wanted) {
        return;
    }

    self->voices = wanted;
    gtk_widget_queue_draw(GTK_WIDGET(self));
    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_VOICES]);

    // No tooltip is pushed here on purpose; see the query handler above.
}

int ts_voice_meter_get_voices(TsVoiceMeter* self)
{
    return self->voices;
}

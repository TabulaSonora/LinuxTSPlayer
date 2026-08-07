#include "app/ts-mpris.hpp"

#include "app/ts-settings.hpp"
#include "app/ts-tone-map.hpp"

namespace {

/// Written out rather than generated: gdbus-codegen is not a build dependency worth adding for two
/// interfaces, and the generated skeleton would still need every property forwarded by hand.
///
/// Only the parts of MPRIS2 this player can honour are declared. There is no playlist, so the track
/// list interface is absent and Next and Previous exist but report themselves unavailable.
constexpr const char* introspection_xml = R"XML(
<node>
  <interface name="org.mpris.MediaPlayer2">
    <method name="Raise"/>
    <method name="Quit"/>
    <property name="CanQuit" type="b" access="read"/>
    <property name="CanRaise" type="b" access="read"/>
    <property name="HasTrackList" type="b" access="read"/>
    <property name="Identity" type="s" access="read"/>
    <property name="DesktopEntry" type="s" access="read"/>
    <property name="SupportedUriSchemes" type="as" access="read"/>
    <property name="SupportedMimeTypes" type="as" access="read"/>
  </interface>
  <interface name="org.mpris.MediaPlayer2.Player">
    <method name="Next"/>
    <method name="Previous"/>
    <method name="Pause"/>
    <method name="PlayPause"/>
    <method name="Stop"/>
    <method name="Play"/>
    <method name="Seek">
      <arg name="Offset" type="x" direction="in"/>
    </method>
    <method name="SetPosition">
      <arg name="TrackId" type="o" direction="in"/>
      <arg name="Position" type="x" direction="in"/>
    </method>
    <method name="OpenUri">
      <arg name="Uri" type="s" direction="in"/>
    </method>
    <signal name="Seeked">
      <arg name="Position" type="x"/>
    </signal>
    <property name="PlaybackStatus" type="s" access="read"/>
    <property name="LoopStatus" type="s" access="readwrite"/>
    <property name="Rate" type="d" access="readwrite"/>
    <property name="Metadata" type="a{sv}" access="read"/>
    <property name="Volume" type="d" access="readwrite"/>
    <property name="Position" type="x" access="read"/>
    <property name="MinimumRate" type="d" access="read"/>
    <property name="MaximumRate" type="d" access="read"/>
    <property name="CanGoNext" type="b" access="read"/>
    <property name="CanGoPrevious" type="b" access="read"/>
    <property name="CanPlay" type="b" access="read"/>
    <property name="CanPause" type="b" access="read"/>
    <property name="CanSeek" type="b" access="read"/>
    <property name="CanControl" type="b" access="read"/>
  </interface>
</node>
)XML";

constexpr const char* object_path = "/org/mpris/MediaPlayer2";
constexpr const char* player_interface = "org.mpris.MediaPlayer2.Player";

} // namespace

struct _TsMpris {
    GObject parent_instance;

    TsPlayerModel* model;
    GtkWindow* window;

    guint name_id;
    guint root_id;
    guint player_id;

    GDBusConnection* connection;
    GDBusNodeInfo* introspection;

    /// A new object path per song, because MPRIS identifies a track by one and a client caches
    /// metadata against it.
    guint track_serial;
};

G_DEFINE_FINAL_TYPE(TsMpris, ts_mpris, G_TYPE_OBJECT)

// -- Property values -------------------------------------------------------------------------------

namespace {

GVariant* string_array(const char* const* strings)
{
    return g_variant_new_strv(strings, -1);
}

GVariant* metadata_for(TsMpris* self)
{
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));

    g_autofree char* song = nullptr;
    double duration = 0.0;
    g_object_get(self->model, "song-name", &song, "duration", &duration, nullptr);

    g_autofree char* track = g_strdup_printf("/co/losno/TabulaSonoraPlayer/Track/%u",
                                             self->track_serial);
    g_variant_builder_add(&builder, "{sv}", "mpris:trackid",
                          g_variant_new_object_path(song != nullptr
                                                        ? track
                                                        : "/org/mpris/MediaPlayer2/TrackList/NoTrack"));

    if (song != nullptr) {
        // The extension is noise in a shell notification; the name without it is the title.
        g_autofree char* title = g_strdup(song);
        if (char* dot = g_strrstr(title, ".")) {
            *dot = '\0';
        }
        g_variant_builder_add(&builder, "{sv}", "xesam:title", g_variant_new_string(title));
    }

    // The module standing in for the artist, as the Apple build does: it is the thing that gives a
    // rendition of a MIDI file its character, and there is no performer to name.
    const TSEngineSettings settings = ts_player_model_get_settings(self->model);
    const char* module = ts_tone_map_display_name(static_cast<int>(settings.map));
    const char* artists[] = {module, nullptr};
    g_variant_builder_add(&builder, "{sv}", "xesam:artist", string_array(artists));

    g_variant_builder_add(&builder, "{sv}", "xesam:album", g_variant_new_string("Tabula Sonora"));

    if (duration > 0.0) {
        g_variant_builder_add(&builder, "{sv}", "mpris:length",
                              g_variant_new_int64(static_cast<gint64>(duration * 1e6)));
    }

    return g_variant_builder_end(&builder);
}

GVariant* playback_status(TsMpris* self)
{
    g_autofree char* song = nullptr;
    gboolean playing = FALSE;
    g_object_get(self->model, "song-name", &song, "playing", &playing, nullptr);

    if (song == nullptr) {
        return g_variant_new_string("Stopped");
    }
    return g_variant_new_string(playing ? "Playing" : "Paused");
}

} // namespace

// -- Method and property handlers ------------------------------------------------------------------

static void ts_mpris_root_method(GDBusConnection*, const char*, const char*, const char*,
                                 const char* method, GVariant*, GDBusMethodInvocation* invocation,
                                 gpointer user_data)
{
    auto* self = TS_MPRIS(user_data);

    if (g_strcmp0(method, "Raise") == 0) {
        if (self->window != nullptr) {
            gtk_window_present(self->window);
        }
    } else if (g_strcmp0(method, "Quit") == 0) {
        if (self->window != nullptr) {
            gtk_window_close(self->window);
        }
    }

    g_dbus_method_invocation_return_value(invocation, nullptr);
}

static GVariant* ts_mpris_root_get(GDBusConnection*, const char*, const char*, const char*,
                                   const char* property, GError**, gpointer)
{
    if (g_strcmp0(property, "CanQuit") == 0 || g_strcmp0(property, "CanRaise") == 0) {
        return g_variant_new_boolean(TRUE);
    }
    if (g_strcmp0(property, "HasTrackList") == 0) {
        return g_variant_new_boolean(FALSE);
    }
    if (g_strcmp0(property, "Identity") == 0) {
        return g_variant_new_string("Tabula Sonora");
    }
    if (g_strcmp0(property, "DesktopEntry") == 0) {
        return g_variant_new_string(TSGUI_APP_ID);
    }
    if (g_strcmp0(property, "SupportedUriSchemes") == 0) {
        const char* schemes[] = {"file", nullptr};
        return string_array(schemes);
    }
    if (g_strcmp0(property, "SupportedMimeTypes") == 0) {
        const char* types[] = {"audio/midi", "audio/x-midi", nullptr};
        return string_array(types);
    }
    return nullptr;
}

static void ts_mpris_player_method(GDBusConnection*, const char*, const char*, const char*,
                                   const char* method, GVariant* parameters,
                                   GDBusMethodInvocation* invocation, gpointer user_data)
{
    auto* self = TS_MPRIS(user_data);

    if (g_strcmp0(method, "Play") == 0) {
        ts_player_model_play(self->model);
    } else if (g_strcmp0(method, "Pause") == 0) {
        ts_player_model_pause(self->model);
    } else if (g_strcmp0(method, "PlayPause") == 0) {
        ts_player_model_toggle_playing(self->model);
    } else if (g_strcmp0(method, "Stop") == 0) {
        // No stopped state of its own: pausing at the start is what stop means here.
        ts_player_model_pause(self->model);
        ts_player_model_seek(self->model, 0.0);
    } else if (g_strcmp0(method, "Seek") == 0) {
        gint64 offset = 0;
        g_variant_get(parameters, "(x)", &offset);

        double position = 0.0;
        g_object_get(self->model, "position", &position, nullptr);
        ts_player_model_seek(self->model, position + static_cast<double>(offset) / 1e6);
    } else if (g_strcmp0(method, "SetPosition") == 0) {
        const char* track = nullptr;
        gint64 position = 0;
        g_variant_get(parameters, "(&ox)", &track, &position);
        ts_player_model_seek(self->model, static_cast<double>(position) / 1e6);
    } else if (g_strcmp0(method, "OpenUri") == 0) {
        // Declared because MPRIS expects it, but opening is the window's business and there is no
        // sensible way to reach it from here without a playlist to put the file into.
        g_dbus_method_invocation_return_error_literal(invocation, G_DBUS_ERROR,
                                                      G_DBUS_ERROR_NOT_SUPPORTED,
                                                      "Open files through the application");
        return;
    }

    g_dbus_method_invocation_return_value(invocation, nullptr);
}

static GVariant* ts_mpris_player_get(GDBusConnection*, const char*, const char*, const char*,
                                     const char* property, GError**, gpointer user_data)
{
    auto* self = TS_MPRIS(user_data);

    if (g_strcmp0(property, "PlaybackStatus") == 0) {
        return playback_status(self);
    }
    if (g_strcmp0(property, "LoopStatus") == 0) {
        gboolean looping = FALSE;
        g_object_get(self->model, "looping", &looping, nullptr);
        // "Track", not "Playlist": the repeat is the file's own loop points, not a queue wrapping.
        return g_variant_new_string(looping ? "Track" : "None");
    }
    if (g_strcmp0(property, "Metadata") == 0) {
        return metadata_for(self);
    }
    if (g_strcmp0(property, "Position") == 0) {
        double position = 0.0;
        g_object_get(self->model, "position", &position, nullptr);
        return g_variant_new_int64(static_cast<gint64>(position * 1e6));
    }
    if (g_strcmp0(property, "Rate") == 0 || g_strcmp0(property, "MinimumRate") == 0
        || g_strcmp0(property, "MaximumRate") == 0) {
        // The engine renders at one speed; a rate control would have to resample the output.
        return g_variant_new_double(1.0);
    }
    if (g_strcmp0(property, "Volume") == 0) {
        return g_variant_new_double(ts_player_model_get_settings(self->model).outputGain);
    }
    if (g_strcmp0(property, "CanGoNext") == 0 || g_strcmp0(property, "CanGoPrevious") == 0) {
        return g_variant_new_boolean(FALSE);
    }
    if (g_strcmp0(property, "CanPlay") == 0 || g_strcmp0(property, "CanPause") == 0
        || g_strcmp0(property, "CanSeek") == 0) {
        g_autofree char* song = nullptr;
        g_object_get(self->model, "song-name", &song, nullptr);
        return g_variant_new_boolean(song != nullptr);
    }
    if (g_strcmp0(property, "CanControl") == 0) {
        return g_variant_new_boolean(TRUE);
    }
    return nullptr;
}

static gboolean ts_mpris_player_set(GDBusConnection*, const char*, const char*, const char*,
                                    const char* property, GVariant* value, GError**,
                                    gpointer user_data)
{
    auto* self = TS_MPRIS(user_data);

    if (g_strcmp0(property, "LoopStatus") == 0) {
        const char* status = g_variant_get_string(value, nullptr);
        g_object_set(self->model, "looping", g_strcmp0(status, "None") != 0, nullptr);
        return TRUE;
    }
    if (g_strcmp0(property, "Volume") == 0) {
        // Straight to the stored setting, so it persists and rebuilds nothing: output gain is
        // applied live.
        g_settings_set_double(ts_settings_get(), "output-gain",
                              CLAMP(g_variant_get_double(value), 0.0, 2.0));
        return TRUE;
    }
    return TRUE;
}

// -- Announcing changes ----------------------------------------------------------------------------

static void ts_mpris_emit(TsMpris* self, const char* property, GVariant* value)
{
    if (self->connection == nullptr) {
        return;
    }

    GVariantBuilder changed;
    g_variant_builder_init(&changed, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&changed, "{sv}", property, value);

    g_dbus_connection_emit_signal(
        self->connection, nullptr, object_path, "org.freedesktop.DBus.Properties",
        "PropertiesChanged",
        g_variant_new("(sa{sv}as)", player_interface, &changed, nullptr), nullptr);
}

static void on_playing_changed(GObject*, GParamSpec*, gpointer user_data)
{
    auto* self = TS_MPRIS(user_data);
    ts_mpris_emit(self, "PlaybackStatus", playback_status(self));
}

static void on_song_changed(GObject*, GParamSpec*, gpointer user_data)
{
    auto* self = TS_MPRIS(user_data);
    ++self->track_serial;
    ts_mpris_emit(self, "Metadata", metadata_for(self));
    ts_mpris_emit(self, "PlaybackStatus", playback_status(self));
}

static void on_looping_changed(GObject*, GParamSpec*, gpointer user_data)
{
    auto* self = TS_MPRIS(user_data);
    gboolean looping = FALSE;
    g_object_get(self->model, "looping", &looping, nullptr);
    ts_mpris_emit(self, "LoopStatus", g_variant_new_string(looping ? "Track" : "None"));
}

// -- Registration ----------------------------------------------------------------------------------

static void on_bus_acquired(GDBusConnection* connection, const char*, gpointer user_data)
{
    auto* self = TS_MPRIS(user_data);
    self->connection = G_DBUS_CONNECTION(g_object_ref(connection));

    static const GDBusInterfaceVTable root_vtable = {ts_mpris_root_method, ts_mpris_root_get,
                                                     nullptr, {nullptr, nullptr, nullptr, nullptr,
                                                               nullptr, nullptr, nullptr, nullptr}};
    static const GDBusInterfaceVTable player_vtable = {ts_mpris_player_method, ts_mpris_player_get,
                                                       ts_mpris_player_set,
                                                       {nullptr, nullptr, nullptr, nullptr, nullptr,
                                                        nullptr, nullptr, nullptr}};

    self->root_id = g_dbus_connection_register_object(
        connection, object_path, self->introspection->interfaces[0], &root_vtable, self, nullptr,
        nullptr);

    self->player_id = g_dbus_connection_register_object(
        connection, object_path, self->introspection->interfaces[1], &player_vtable, self, nullptr,
        nullptr);
}

static void ts_mpris_dispose(GObject* object)
{
    auto* self = TS_MPRIS(object);

    if (self->connection != nullptr) {
        if (self->root_id != 0) {
            g_dbus_connection_unregister_object(self->connection, self->root_id);
            self->root_id = 0;
        }
        if (self->player_id != 0) {
            g_dbus_connection_unregister_object(self->connection, self->player_id);
            self->player_id = 0;
        }
    }

    g_clear_handle_id(&self->name_id, g_bus_unown_name);
    g_clear_pointer(&self->introspection, g_dbus_node_info_unref);
    g_clear_object(&self->connection);
    g_clear_object(&self->model);

    G_OBJECT_CLASS(ts_mpris_parent_class)->dispose(object);
}

static void ts_mpris_class_init(TsMprisClass* klass)
{
    G_OBJECT_CLASS(klass)->dispose = ts_mpris_dispose;
}

static void ts_mpris_init(TsMpris*) {}

TsMpris* ts_mpris_new(TsPlayerModel* model, GtkWindow* window)
{
    auto* self = TS_MPRIS(g_object_new(TS_TYPE_MPRIS, nullptr));
    self->model = TS_PLAYER_MODEL(g_object_ref(model));
    self->window = window;

    g_autoptr(GError) error = nullptr;
    self->introspection = g_dbus_node_info_new_for_xml(introspection_xml, &error);
    if (self->introspection == nullptr) {
        g_warning("MPRIS introspection is malformed: %s", error->message);
        return self;
    }

    // Losing the name is not worth reporting: a second instance is a perfectly ordinary thing and
    // the first one keeps the controls.
    self->name_id = g_bus_own_name(G_BUS_TYPE_SESSION,
                                   "org.mpris.MediaPlayer2.TabulaSonoraPlayer",
                                   G_BUS_NAME_OWNER_FLAGS_NONE, on_bus_acquired, nullptr, nullptr,
                                   self, nullptr);

    // On state changes only. Position is deliberately absent: MPRIS clients extrapolate it from
    // the rate and ask for it when they need it, and announcing it ten times a second would wake
    // every listener on the bus for nothing.
    g_signal_connect_object(model, "notify::playing", G_CALLBACK(on_playing_changed), self,
                            G_CONNECT_DEFAULT);
    g_signal_connect_object(model, "notify::song-name", G_CALLBACK(on_song_changed), self,
                            G_CONNECT_DEFAULT);
    g_signal_connect_object(model, "notify::looping", G_CALLBACK(on_looping_changed), self,
                            G_CONNECT_DEFAULT);

    return self;
}

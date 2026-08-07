#include "app/ts-settings.hpp"

GSettings* ts_settings_get(void)
{
    static GSettings* settings = nullptr;
    if (settings == nullptr) {
        settings = g_settings_new(TSGUI_APP_ID);
    }
    return settings;
}

namespace {

/// Everything that lives in the generator's construction options, gathered into one struct so the
/// engine rebuilds once however many keys changed.
///
/// The three that are not construction options -- looping, buffer and MIDI auto-connect -- are
/// applied beside it, because none of them costs a rebuild and the buffer in particular must not:
/// the ring is sized once and this only moves the fill target inside it.
void apply(GSettings* settings, TsPlayerModel* model)
{
    TSEngineSettings engine{};
    engine.map = static_cast<TSToneMap>(g_settings_get_int(settings, "map"));
    engine.polyphony = g_settings_get_int(settings, "polyphony");
    engine.ports = g_settings_get_int(settings, "ports");
    engine.reverb = g_settings_get_boolean(settings, "reverb");
    engine.chorus = g_settings_get_boolean(settings, "chorus");
    engine.delay = g_settings_get_boolean(settings, "delay");
    engine.efx = g_settings_get_boolean(settings, "efx");
    engine.outputGain = g_settings_get_double(settings, "output-gain");

    ts_player_model_apply_settings(model, engine);

    g_object_set(model,
                 "looping", g_settings_get_boolean(settings, "looping"),
                 "latency-ms", g_settings_get_int(settings, "latency-ms"),
                 nullptr);

    ts_player_model_set_midi_auto_connect(model,
                                          g_settings_get_boolean(settings, "midi-auto-connect"));
}

void on_settings_changed(GSettings* settings, const char* key, gpointer user_data)
{
    // Window geometry and the ROM's verified flag are stored here too and mean nothing to the
    // engine; rebuilding the generator when the window is resized would be an audible bug.
    if (g_str_has_prefix(key, "window-") || g_strcmp0(key, "rom-verified") == 0) {
        return;
    }

    apply(settings, TS_PLAYER_MODEL(user_data));
}

} // namespace

void ts_settings_bind_model(GSettings* settings, TsPlayerModel* model)
{
    g_signal_connect_object(settings, "changed", G_CALLBACK(on_settings_changed), model,
                            G_CONNECT_DEFAULT);
    apply(settings, model);

    // Looping is the one engine-side value the interface can also change directly, from the
    // transport's own toggle, so it is written back rather than only read.
    g_settings_bind(settings, "looping", model, "looping", G_SETTINGS_BIND_DEFAULT);
}

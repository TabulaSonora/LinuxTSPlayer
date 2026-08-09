#include "app/ts-player-model.hpp"

#include "app/ts-part-object.hpp"
#include "host/ts_audio_device.hpp"
#include "host/ts_midi_input.hpp"
#include "host/ts_player.hpp"

#include "tabulasonora/rom_image.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <exception>
#include <string>
#include <type_traits>

namespace {

/// Ten times a second, matching the Apple front end. The render thread publishes five times faster
/// than this; the extra resolution would not survive a display refresh.
constexpr guint tick_interval_ms = 100;

} // namespace

struct _TsPlayerModel {
    GObject parent_instance;

    // Held by pointer rather than by value: a GObject instance struct is allocated by GLib, which
    // does not run C++ constructors.
    ts::host::Player* player;
    ts::host::AudioDevice* device;
    ts::host::MidiInput* midi;

    GListStore* parts;

    guint tick;

    char* rom_name;
    char* song_name;

    gboolean playing;
    gboolean looping;
    gboolean complete;
    gboolean xg_mode;
    gboolean exporting;

    double position;
    double duration;
    double export_progress;

    int active_voices;
    int voice_capacity;
    int latency_ms;
    gint64 underruns;

    /// Read by the export worker and written by the main thread; not a property, because the
    /// progress it feeds is.
    std::atomic<double>* progress_slot;

    /// What the loaded file says about itself, cached at load. Held by pointer for the same reason
    /// the engine objects above are: GLib allocates this struct without running a constructor.
    ts::host::SongInfo* song_info;

    /// One bit per part, so a change in *which* parts the song addresses can be spotted without
    /// comparing sixty-four structs.
    guint64 presence;

    /// The channel each part listens on, as of the last tick. Six bits per part is too much to
    /// pack into a word the way presence is, and this is compared rather than read.
    signed char routing[TS_MAX_PARTS];
};

G_DEFINE_FINAL_TYPE(TsPlayerModel, ts_player_model, G_TYPE_OBJECT)

enum {
    PROP_ROM_NAME = 1,
    PROP_SONG_NAME,
    PROP_PLAYING,
    PROP_LOOPING,
    PROP_COMPLETE,
    PROP_XG_MODE,
    PROP_EXPORTING,
    PROP_POSITION,
    PROP_DURATION,
    PROP_EXPORT_PROGRESS,
    PROP_ACTIVE_VOICES,
    PROP_VOICE_CAPACITY,
    PROP_LATENCY_MS,
    PROP_UNDERRUNS,
    N_PROPS,
};

static GParamSpec* properties[N_PROPS];

enum { SIGNAL_PRESENCE_CHANGED, SIGNAL_ROUTING_CHANGED, N_SIGNALS };
static guint signals[N_SIGNALS];

// -- Property plumbing -----------------------------------------------------------------------------

namespace {

/// The value is deduced from the field, never from itself: GLib's TRUE and FALSE are `bool` in C++
/// while a `gboolean` field is `int`, so deducing from both arguments fails on every flag here.
template <typename T>
void set_field(TsPlayerModel* self, T& field, std::type_identity_t<T> value, int prop)
{
    if (field != value) {
        field = value;
        g_object_notify_by_pspec(G_OBJECT(self), properties[prop]);
    }
}

void set_string_field(TsPlayerModel* self, char*& field, const std::string& value, int prop)
{
    const char* wanted = value.empty() ? nullptr : value.c_str();
    if (g_strcmp0(field, wanted) != 0) {
        g_free(field);
        field = g_strdup(wanted);
        g_object_notify_by_pspec(G_OBJECT(self), properties[prop]);
    }
}

} // namespace

// -- The tick ----------------------------------------------------------------------------------

static void ts_player_model_refresh(TsPlayerModel* self)
{
    const auto snapshot = self->player->snapshot();
    const auto rate = static_cast<double>(ts::host::Session::sample_rate);

    set_field(self, self->position, static_cast<double>(snapshot.position) / rate, PROP_POSITION);
    set_field(self, self->duration, static_cast<double>(snapshot.length) / rate, PROP_DURATION);
    set_field(self, self->active_voices, snapshot.activeVoices, PROP_ACTIVE_VOICES);
    set_field(self, self->voice_capacity, snapshot.voiceCapacity, PROP_VOICE_CAPACITY);
    set_field(self, self->xg_mode, snapshot.xgMode ? TRUE : FALSE, PROP_XG_MODE);
    set_field(self, self->underruns, static_cast<gint64>(snapshot.underruns), PROP_UNDERRUNS);

    if (self->exporting) {
        set_field(self, self->export_progress,
                  self->progress_slot->load(std::memory_order_relaxed), PROP_EXPORT_PROGRESS);
    }

    // Solo is a property of the whole mixer, not of one strip: a part is dimmed when something
    // *else* is soloed.
    const bool any_soloed = std::any_of(snapshot.parts.begin(), snapshot.parts.end(),
                                        [](const auto& part) { return part.soloed; });

    guint64 presence = 0;
    signed char routing[TS_MAX_PARTS] = {};
    const guint count = g_list_model_get_n_items(G_LIST_MODEL(self->parts));
    for (guint i = 0; i < count; ++i) {
        auto* part = TS_PART(g_list_model_get_item(G_LIST_MODEL(self->parts), i));
        const auto& state = snapshot.parts[i];
        ts_part_update(part, state, (any_soloed && !state.soloed) ? TRUE : FALSE);
        if (state.present) {
            presence |= guint64{1} << i;
        }
        routing[i] = static_cast<signed char>(ts_part_get_channel(part));
        g_object_unref(part);
    }

    // A filter over the parts has no reason to re-run on its own: the list never changes length,
    // only the items' properties change, which GtkFilterListModel does not watch. Announcing it
    // here is what lets the mixer invalidate its filter, and only when the set actually differs.
    if (presence != self->presence) {
        self->presence = presence;
        g_signal_emit(self, signals[SIGNAL_PRESENCE_CHANGED], 0);
    }

    // And the same again for the order, which a sorter watches no more closely than a filter
    // watches the filtered property. A bulk dump can move a part without changing which parts are
    // addressed at all, so this is a separate question from presence and asked separately.
    if (memcmp(routing, self->routing, sizeof(routing)) != 0) {
        memcpy(self->routing, routing, sizeof(routing));
        g_signal_emit(self, signals[SIGNAL_ROUTING_CHANGED], 0);
    }

    // Reaching the end pauses rather than spinning at the tail, exactly as the Apple build does.
    if (snapshot.complete && !self->complete) {
        set_field(self, self->complete, TRUE, PROP_COMPLETE);
        ts_player_model_pause(self);
    } else if (!snapshot.complete && self->complete) {
        set_field(self, self->complete, FALSE, PROP_COMPLETE);
    }
}

static gboolean ts_player_model_tick(gpointer user_data)
{
    ts_player_model_refresh(TS_PLAYER_MODEL(user_data));
    return G_SOURCE_CONTINUE;
}

static void ts_player_model_start_ticking(TsPlayerModel* self)
{
    if (self->tick == 0) {
        self->tick = g_timeout_add(tick_interval_ms, ts_player_model_tick, self);
    }
}

// -- Loading -----------------------------------------------------------------------------------

gboolean ts_player_model_load_rom(TsPlayerModel* self, const char* path, gboolean verify_fully,
                                  GError** error)
{
    try {
        self->player->load_rom(path, verify_fully != FALSE);
    } catch (const ts::RomIdentityError& wrong) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, wrong.what());
        return FALSE;
    } catch (const std::exception& failure) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, failure.what());
        return FALSE;
    }

    set_string_field(self, self->rom_name, self->player->rom_name(), PROP_ROM_NAME);

    try {
        self->device->start();
    } catch (const std::exception& failure) {
        g_warning("No audio output: %s", failure.what());
    }

    ts_player_model_start_ticking(self);
    ts_player_model_refresh(self);
    return TRUE;
}

gboolean ts_player_model_load_song(TsPlayerModel* self, const char* path, GError** error)
{
    try {
        self->player->load_song(path);
    } catch (const std::exception& failure) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, failure.what());
        return FALSE;
    }

    // Before the name is published, because `notify::song-name` is what tells an open information
    // window to redraw itself and it must not read the previous file's metadata when it does.
    *self->song_info = self->player->song_info();

    set_string_field(self, self->song_name, self->player->song_name(), PROP_SONG_NAME);
    set_field(self, self->complete, FALSE, PROP_COMPLETE);

    ts_player_model_start_ticking(self);

    // Synchronously, so the scrubber has a duration before the window is drawn rather than a tenth
    // of a second later.
    ts_player_model_refresh(self);
    return TRUE;
}

const ts::host::SongInfo& ts_player_model_get_song_info(TsPlayerModel* self)
{
    return *self->song_info;
}

void ts_player_model_unload_song(TsPlayerModel* self)
{
    ts_player_model_pause(self);
    self->player->unload_song();
    *self->song_info = {};
    set_string_field(self, self->song_name, {}, PROP_SONG_NAME);
    ts_player_model_refresh(self);
}

// -- Transport ---------------------------------------------------------------------------------

void ts_player_model_play(TsPlayerModel* self)
{
    // Pressing play on a finished song starts it again rather than doing nothing.
    if (self->complete) {
        self->player->seek(0);
        set_field(self, self->complete, FALSE, PROP_COMPLETE);
    }

    self->player->set_paused(false);
    set_field(self, self->playing, TRUE, PROP_PLAYING);
}

void ts_player_model_pause(TsPlayerModel* self)
{
    self->player->set_paused(true);
    set_field(self, self->playing, FALSE, PROP_PLAYING);
}

void ts_player_model_toggle_playing(TsPlayerModel* self)
{
    if (self->playing) {
        ts_player_model_pause(self);
    } else {
        ts_player_model_play(self);
    }
}

void ts_player_model_seek(TsPlayerModel* self, double seconds)
{
    const auto frame = static_cast<std::int64_t>(
        std::max(0.0, seconds) * static_cast<double>(ts::host::Session::sample_rate));
    self->player->seek(frame);

    // Optimistically, so a released slider stays where it was put instead of snapping back for the
    // tenth of a second before the render thread reports the new position.
    set_field(self, self->position, std::max(0.0, seconds), PROP_POSITION);
    set_field(self, self->complete, FALSE, PROP_COMPLETE);
}

void ts_player_model_panic(TsPlayerModel* self)
{
    self->player->panic();
}

// -- Mixer -------------------------------------------------------------------------------------

GListModel* ts_player_model_get_parts(TsPlayerModel* self)
{
    return G_LIST_MODEL(self->parts);
}

void ts_player_model_set_muted(TsPlayerModel* self, int part, gboolean muted)
{
    self->player->set_muted(part, muted != FALSE);
    ts_player_model_refresh(self);
}

void ts_player_model_set_soloed(TsPlayerModel* self, int part, gboolean soloed)
{
    self->player->set_soloed(part, soloed != FALSE);
    ts_player_model_refresh(self);
}

void ts_player_model_reset_channels(TsPlayerModel* self)
{
    self->player->reset_channels();
    ts_player_model_refresh(self);
}

void ts_player_model_send_control(TsPlayerModel* self, int port, int channel, int controller,
                                  int value)
{
    self->player->send_channel(port, 0xB0 | (channel & 0x0F), controller, value);

    // No refresh here, unlike mute and solo above. Those write a mask the render thread reads
    // directly, so a snapshot taken immediately after already reflects them. This queues a message
    // in the live-MIDI inbox that is not applied until the next block, so refreshing now would
    // publish the value from *before* the send -- and a fader bound to that would visibly jump back
    // under the pointer. The next tick reports it, some tens of milliseconds later.
}

// -- Settings ----------------------------------------------------------------------------------

TSEngineSettings ts_player_model_get_settings(TsPlayerModel* self)
{
    return self->player->settings();
}

void ts_player_model_apply_settings(TsPlayerModel* self, TSEngineSettings settings)
{
    self->player->set_settings(settings);
    ts_player_model_refresh(self);
}

void ts_player_model_set_midi_auto_connect(TsPlayerModel* self, gboolean enabled)
{
    self->midi->set_auto_connect(enabled != FALSE);
}

// -- Export ------------------------------------------------------------------------------------

namespace {

struct ExportRequest {
    TsPlayerModel* model;
    std::string path;
};

void export_worker(GTask* task, gpointer source, gpointer task_data, GCancellable* cancellable)
{
    (void)source;
    auto* request = static_cast<ExportRequest*>(task_data);
    auto* self = request->model;

    try {
        self->player->export_wav(request->path, [&](double fraction) {
            self->progress_slot->store(fraction, std::memory_order_relaxed);
            // Returning false aborts the render, which is how Cancel reaches a loop that is deep
            // inside the engine.
            return !g_cancellable_is_cancelled(cancellable);
        });
    } catch (const std::exception& failure) {
        g_task_return_new_error_literal(task, G_FILE_ERROR, G_FILE_ERROR_FAILED, failure.what());
        return;
    }

    if (g_cancellable_is_cancelled(cancellable)) {
        g_task_return_error_if_cancelled(task);
        return;
    }

    g_task_return_boolean(task, TRUE);
}

void export_request_free(gpointer data)
{
    delete static_cast<ExportRequest*>(data);
}

} // namespace

void ts_player_model_export_wav(TsPlayerModel* self, const char* path, GCancellable* cancellable,
                                GAsyncReadyCallback callback, gpointer user_data)
{
    self->progress_slot->store(0.0, std::memory_order_relaxed);
    set_field(self, self->export_progress, 0.0, PROP_EXPORT_PROGRESS);
    set_field(self, self->exporting, TRUE, PROP_EXPORTING);

    GTask* task = g_task_new(self, cancellable, callback, user_data);
    g_task_set_task_data(task, new ExportRequest{self, path}, export_request_free);
    g_task_run_in_thread(task, export_worker);
    g_object_unref(task);
}

gboolean ts_player_model_export_wav_finish(TsPlayerModel* self, GAsyncResult* result,
                                           GError** error)
{
    set_field(self, self->exporting, FALSE, PROP_EXPORTING);
    return g_task_propagate_boolean(G_TASK(result), error);
}

// -- GObject -------------------------------------------------------------------------------------

static void ts_player_model_get_property(GObject* object, guint id, GValue* value, GParamSpec* spec)
{
    auto* self = TS_PLAYER_MODEL(object);

    switch (id) {
    case PROP_ROM_NAME: g_value_set_string(value, self->rom_name); break;
    case PROP_SONG_NAME: g_value_set_string(value, self->song_name); break;
    case PROP_PLAYING: g_value_set_boolean(value, self->playing); break;
    case PROP_LOOPING: g_value_set_boolean(value, self->looping); break;
    case PROP_COMPLETE: g_value_set_boolean(value, self->complete); break;
    case PROP_XG_MODE: g_value_set_boolean(value, self->xg_mode); break;
    case PROP_EXPORTING: g_value_set_boolean(value, self->exporting); break;
    case PROP_POSITION: g_value_set_double(value, self->position); break;
    case PROP_DURATION: g_value_set_double(value, self->duration); break;
    case PROP_EXPORT_PROGRESS: g_value_set_double(value, self->export_progress); break;
    case PROP_ACTIVE_VOICES: g_value_set_int(value, self->active_voices); break;
    case PROP_VOICE_CAPACITY: g_value_set_int(value, self->voice_capacity); break;
    case PROP_LATENCY_MS: g_value_set_int(value, self->latency_ms); break;
    case PROP_UNDERRUNS: g_value_set_int64(value, self->underruns); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID(object, id, spec); break;
    }
}

static void ts_player_model_set_property(GObject* object, guint id, const GValue* value,
                                         GParamSpec* spec)
{
    auto* self = TS_PLAYER_MODEL(object);

    switch (id) {
    case PROP_LOOPING: {
        const gboolean wanted = g_value_get_boolean(value);
        if (self->looping != wanted) {
            self->looping = wanted;
            self->player->set_looping(wanted != FALSE);
            g_object_notify_by_pspec(object, properties[PROP_LOOPING]);
        }
        break;
    }
    case PROP_LATENCY_MS: {
        const int wanted = g_value_get_int(value);
        if (self->latency_ms != wanted) {
            self->latency_ms = wanted;
            self->player->set_latency_ms(wanted);
            g_object_notify_by_pspec(object, properties[PROP_LATENCY_MS]);
        }
        break;
    }
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID(object, id, spec); break;
    }
}

static void ts_player_model_dispose(GObject* object)
{
    auto* self = TS_PLAYER_MODEL(object);

    g_clear_handle_id(&self->tick, g_source_remove);

    // Reverse of construction: stop taking input, stop the device, then tear down the engine the
    // other two were pointing at.
    delete self->midi;
    self->midi = nullptr;
    delete self->device;
    self->device = nullptr;
    delete self->player;
    self->player = nullptr;
    delete self->progress_slot;
    self->progress_slot = nullptr;
    delete self->song_info;
    self->song_info = nullptr;

    g_clear_object(&self->parts);

    G_OBJECT_CLASS(ts_player_model_parent_class)->dispose(object);
}

static void ts_player_model_finalize(GObject* object)
{
    auto* self = TS_PLAYER_MODEL(object);
    g_clear_pointer(&self->rom_name, g_free);
    g_clear_pointer(&self->song_name, g_free);
    G_OBJECT_CLASS(ts_player_model_parent_class)->finalize(object);
}

static void ts_player_model_class_init(TsPlayerModelClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = ts_player_model_dispose;
    object_class->finalize = ts_player_model_finalize;
    object_class->get_property = ts_player_model_get_property;
    object_class->set_property = ts_player_model_set_property;

    const auto read_only = static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    const auto read_write = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_ROM_NAME] = g_param_spec_string("rom-name", nullptr, nullptr, nullptr, read_only);
    properties[PROP_SONG_NAME] = g_param_spec_string("song-name", nullptr, nullptr, nullptr, read_only);
    properties[PROP_PLAYING] = g_param_spec_boolean("playing", nullptr, nullptr, FALSE, read_only);
    properties[PROP_LOOPING] = g_param_spec_boolean("looping", nullptr, nullptr, FALSE, read_write);
    properties[PROP_COMPLETE] = g_param_spec_boolean("complete", nullptr, nullptr, FALSE, read_only);
    properties[PROP_XG_MODE] = g_param_spec_boolean("xg-mode", nullptr, nullptr, FALSE, read_only);
    properties[PROP_EXPORTING] = g_param_spec_boolean("exporting", nullptr, nullptr, FALSE, read_only);
    properties[PROP_POSITION] = g_param_spec_double("position", nullptr, nullptr, 0, G_MAXDOUBLE, 0, read_only);
    properties[PROP_DURATION] = g_param_spec_double("duration", nullptr, nullptr, 0, G_MAXDOUBLE, 0, read_only);
    properties[PROP_EXPORT_PROGRESS] = g_param_spec_double("export-progress", nullptr, nullptr, 0, 1, 0, read_only);
    properties[PROP_ACTIVE_VOICES] = g_param_spec_int("active-voices", nullptr, nullptr, 0, G_MAXINT, 0, read_only);
    properties[PROP_VOICE_CAPACITY] = g_param_spec_int("voice-capacity", nullptr, nullptr, 0, G_MAXINT, 0, read_only);
    properties[PROP_LATENCY_MS] = g_param_spec_int(
        "latency-ms", nullptr, nullptr, ts::host::Player::min_latency_ms,
        ts::host::Player::max_latency_ms, ts::host::Player::default_latency_ms, read_write);
    properties[PROP_UNDERRUNS] = g_param_spec_int64("underruns", nullptr, nullptr, 0, G_MAXINT64, 0, read_only);

    g_object_class_install_properties(object_class, N_PROPS, properties);

    signals[SIGNAL_PRESENCE_CHANGED] =
        g_signal_new("presence-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, nullptr,
                     nullptr, nullptr, G_TYPE_NONE, 0);

    signals[SIGNAL_ROUTING_CHANGED] =
        g_signal_new("routing-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, nullptr,
                     nullptr, nullptr, G_TYPE_NONE, 0);
}

static void ts_player_model_init(TsPlayerModel* self)
{
    self->player = new ts::host::Player();
    self->device = new ts::host::AudioDevice(*self->player);
    self->midi = new ts::host::MidiInput(*self->player);
    self->progress_slot = new std::atomic<double>{0.0};
    self->song_info = new ts::host::SongInfo{};

    self->latency_ms = self->player->latency_ms();

    // The sequencer port is opened up front rather than after a ROM loads, unlike the Apple build's
    // CoreMIDI client. A port that does not exist cannot be wired up, and a person setting up a
    // keyboard should not have to load a song first to find somewhere to plug it into. Notes that
    // arrive before there is an engine are dropped by the session, which is the right answer.
    self->midi->start();

    // All sixty-four, created once and never replaced. The mixer filters them down to the parts the
    // song addresses; updating in place is what keeps rows from being rebuilt ten times a second.
    self->parts = g_list_store_new(TS_TYPE_PART);
    for (int i = 0; i < TS_MAX_PARTS; ++i) {
        TsPart* part = ts_part_new(i);
        g_list_store_append(self->parts, part);
        g_object_unref(part);
    }
}

TsPlayerModel* ts_player_model_new(void)
{
    return TS_PLAYER_MODEL(g_object_new(TS_TYPE_PLAYER_MODEL, nullptr));
}

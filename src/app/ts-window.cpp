#include "app/ts-window.hpp"

#include "app/ts-mixer.hpp"
#include "app/ts-mpris.hpp"
#include "app/ts-player-model.hpp"
#include "app/ts-prefs-dialog.hpp"
#include "app/ts-rom-setup.hpp"
#include "app/ts-settings.hpp"
#include "app/ts-transport.hpp"

#include <initializer_list>
#include <string>

struct _TsWindow {
    AdwApplicationWindow parent_instance;

    TsPlayerModel* model;
    GSettings* settings;

    GtkWidget* screens;
    GtkWidget* rom_setup;
    AdwWindowTitle* window_title;
    AdwMultiLayoutView* layouts;

    GtkWidget* transport;
    GtkWidget* mixer;

    GMenu* recent_section;

    TsMpris* mpris;

    /// A file asked for before the ROM finished verifying.
    ///
    /// Opening one is not an error at that point, it is just early: the 27 MB hash takes a moment
    /// and a double-clicked file should still play once it finishes.
    GFile* pending;

    GCancellable* export_cancellable;
};

G_DEFINE_FINAL_TYPE(TsWindow, ts_window, ADW_TYPE_APPLICATION_WINDOW)

// -- Reporting -----------------------------------------------------------------------------------

static void ts_window_report(TsWindow* self, const char* heading, const char* detail)
{
    AdwDialog* dialog = adw_alert_dialog_new(heading, detail);
    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "ok", "OK");
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "ok");
    adw_dialog_present(dialog, GTK_WIDGET(self));
}

// -- Where the ROM is kept -----------------------------------------------------------------------

namespace {

/// The engine reads the file positionally for the whole session, so it has to be somewhere stable
/// and ours -- not wherever the user happened to pick it from.
GFile* rom_destination()
{
    g_autofree char* dir =
        g_build_filename(g_get_user_data_dir(), "tabula-sonora", nullptr);
    g_mkdir_with_parents(dir, 0700);

    g_autofree char* path = g_build_filename(dir, "SCCore.dll", nullptr);
    return g_file_new_for_path(path);
}

} // namespace

// -- Screens -------------------------------------------------------------------------------------

static void ts_window_update_screen(TsWindow* self)
{
    g_autofree char* rom = nullptr;
    g_autofree char* song = nullptr;
    g_object_get(self->model, "rom-name", &rom, "song-name", &song, nullptr);

    gtk_stack_set_visible_child_name(GTK_STACK(self->screens), rom != nullptr ? "player" : "setup");
    adw_window_title_set_title(self->window_title, song != nullptr ? song : "Tabula Sonora");

    // Export lives in the menu rather than on a button, so this is what greys it out when there is
    // nothing to export.
    if (auto* action = G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(self), "export"))) {
        g_simple_action_set_enabled(action, song != nullptr);
    }
}

static void on_model_notify(GObject*, GParamSpec*, gpointer user_data)
{
    ts_window_update_screen(TS_WINDOW(user_data));
}

// -- Opening songs -------------------------------------------------------------------------------

void ts_window_open_file(TsWindow* self, GFile* file)
{
    g_autofree char* rom = nullptr;
    g_object_get(self->model, "rom-name", &rom, nullptr);

    if (rom == nullptr) {
        g_set_object(&self->pending, file);
        return;
    }

    g_autofree char* path = g_file_get_path(file);
    if (path == nullptr) {
        ts_window_report(self, "That file could not be played",
                         "The engine reads MIDI files from disk, and this one is not on a local "
                         "filesystem.");
        return;
    }

    g_autoptr(GError) error = nullptr;
    if (!ts_player_model_load_song(self->model, path, &error)) {
        ts_window_report(self, "That file could not be played", error->message);
        return;
    }

    g_autofree char* uri = g_file_get_uri(file);
    gtk_recent_manager_add_item(gtk_recent_manager_get_default(), uri);

    // Opening a file starts it, which is what every other player does and what the Apple build
    // does too.
    ts_player_model_play(self->model);
}

// -- Recent files --------------------------------------------------------------------------------

namespace {

/// Whether a recent entry is something this player can open.
///
/// Matched on the extension, for the same reason the file dialog's filter is: most of these formats
/// have no registered MIME type, so the recent manager records them as plain data.
bool is_playable(const char* uri)
{
    static const char* extensions[] = {".mid", ".midi", ".rmi", ".mids", ".mus",  ".xmi",
                                       ".xmf", ".mxmf", ".gmf", ".hmi",  ".hmp",  ".lds"};

    g_autofree char* lowered = g_ascii_strdown(uri, -1);
    for (const char* extension : extensions) {
        if (g_str_has_suffix(lowered, extension)) {
            return true;
        }
    }
    return false;
}

} // namespace

static void ts_window_refresh_recent(TsWindow* self)
{
    g_menu_remove_all(self->recent_section);

    GList* items = gtk_recent_manager_get_items(gtk_recent_manager_get_default());
    // Most recently used first, which is not the order the manager hands them back in.
    items = g_list_sort(items, [](gconstpointer a, gconstpointer b) {
        return gtk_recent_info_get_modified(const_cast<GtkRecentInfo*>(
                   static_cast<const GtkRecentInfo*>(b)))
                   > gtk_recent_info_get_modified(const_cast<GtkRecentInfo*>(
                         static_cast<const GtkRecentInfo*>(a)))
                   ? 1
                   : -1;
    });

    int shown = 0;
    for (GList* node = items; node != nullptr && shown < 10; node = node->next) {
        auto* info = static_cast<GtkRecentInfo*>(node->data);
        const char* uri = gtk_recent_info_get_uri(info);

        if (!is_playable(uri) || !gtk_recent_info_exists(info)) {
            continue;
        }

        g_autofree char* name = g_path_get_basename(gtk_recent_info_get_uri_display(info));
        g_autoptr(GMenuItem) item = g_menu_item_new(name, nullptr);
        g_menu_item_set_action_and_target_value(item, "win.open-recent",
                                                g_variant_new_string(uri));
        g_menu_append_item(self->recent_section, item);
        ++shown;
    }

    g_list_free_full(items, reinterpret_cast<GDestroyNotify>(gtk_recent_info_unref));
}

static void on_open_recent(GSimpleAction*, GVariant* parameter, gpointer user_data)
{
    g_autoptr(GFile) file = g_file_new_for_uri(g_variant_get_string(parameter, nullptr));
    ts_window_open_file(TS_WINDOW(user_data), file);
}

static void on_open_response(GObject* source, GAsyncResult* result, gpointer user_data)
{
    g_autoptr(GError) error = nullptr;
    g_autoptr(GFile) file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, &error);

    if (file != nullptr) {
        ts_window_open_file(TS_WINDOW(user_data), file);
    }
}

static void on_open(GSimpleAction*, GVariant*, gpointer user_data)
{
    auto* self = TS_WINDOW(user_data);

    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Open MIDI File");

    // Matched by extension rather than content type: most of these have no registered MIME type,
    // and the engine sniffs the contents itself on the way in anyway.
    GtkFileFilter* songs = gtk_file_filter_new();
    gtk_file_filter_set_name(songs, "MIDI and legacy music files");
    for (const char* pattern : {"*.mid", "*.midi", "*.rmi", "*.mids", "*.mus", "*.xmi", "*.xmf",
                                "*.mxmf", "*.gmf", "*.hmi", "*.hmp", "*.lds"}) {
        gtk_file_filter_add_pattern(songs, pattern);
    }

    GtkFileFilter* all = gtk_file_filter_new();
    gtk_file_filter_set_name(all, "All files");
    gtk_file_filter_add_pattern(all, "*");

    g_autoptr(GListStore) filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, songs);
    g_list_store_append(filters, all);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    gtk_file_dialog_set_default_filter(dialog, songs);

    gtk_file_dialog_open(dialog, GTK_WINDOW(self), nullptr, on_open_response, self);
    g_object_unref(dialog);
}

// -- Drag and drop -------------------------------------------------------------------------------

static gboolean on_drop(GtkDropTarget*, const GValue* value, double, double, gpointer user_data)
{
    if (!G_VALUE_HOLDS(value, G_TYPE_FILE)) {
        return FALSE;
    }
    ts_window_open_file(TS_WINDOW(user_data), G_FILE(g_value_get_object(value)));
    return TRUE;
}

// -- The ROM -------------------------------------------------------------------------------------

namespace {

struct RomLoad {
    TsWindow* window;
    std::string path;
    bool verify_fully;
};

} // namespace

static void ts_window_rom_loaded(TsWindow* self, gboolean verified_fully, const GError* error)
{
    ts_rom_setup_set_verifying(TS_ROM_SETUP(self->rom_setup), FALSE);

    if (error != nullptr) {
        // A wrong or truncated copy is deleted rather than left to fail identically on every
        // future launch.
        g_autoptr(GFile) destination = rom_destination();
        g_file_delete(destination, nullptr, nullptr);
        g_settings_set_boolean(self->settings, "rom-verified", FALSE);

        ts_window_report(self, "That is not the right SCCore.dll", error->message);
        return;
    }

    if (verified_fully) {
        g_settings_set_boolean(self->settings, "rom-verified", TRUE);
    }

    ts_window_update_screen(self);

    // Anything opened while the hash was running has been waiting for a voice.
    if (self->pending != nullptr) {
        g_autoptr(GFile) file = self->pending;
        self->pending = nullptr;
        ts_window_open_file(self, file);
    }
}

static void rom_load_worker(GTask* task, gpointer, gpointer task_data, GCancellable*)
{
    auto* load = static_cast<RomLoad*>(task_data);

    g_autoptr(GError) error = nullptr;
    // Hashing 27 MB is why this is on a worker at all; the engine build that follows it is quick by
    // comparison but has to happen on the same thread as the load.
    if (!ts_player_model_load_rom(load->window->model, load->path.c_str(),
                                  load->verify_fully ? TRUE : FALSE, &error)) {
        g_task_return_error(task, g_steal_pointer(&error));
        return;
    }

    g_task_return_boolean(task, TRUE);
}

static void on_rom_loaded(GObject*, GAsyncResult* result, gpointer user_data)
{
    auto* self = TS_WINDOW(user_data);
    auto* load = static_cast<RomLoad*>(g_task_get_task_data(G_TASK(result)));
    const gboolean verified_fully = load->verify_fully ? TRUE : FALSE;

    g_autoptr(GError) error = nullptr;
    g_task_propagate_boolean(G_TASK(result), &error);
    ts_window_rom_loaded(self, verified_fully, error);
}

static void ts_window_load_rom_async(TsWindow* self, const char* path, gboolean verify_fully)
{
    ts_rom_setup_set_verifying(TS_ROM_SETUP(self->rom_setup), TRUE);

    GTask* task = g_task_new(self, nullptr, on_rom_loaded, self);
    g_task_set_task_data(task, new RomLoad{self, path, verify_fully != FALSE},
                         [](gpointer data) { delete static_cast<RomLoad*>(data); });
    g_task_run_in_thread(task, rom_load_worker);
    g_object_unref(task);
}

static void on_rom_chosen(GObject* source, GAsyncResult* result, gpointer user_data)
{
    auto* self = TS_WINDOW(user_data);

    g_autoptr(GError) error = nullptr;
    g_autoptr(GFile) picked = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, &error);
    if (picked == nullptr) {
        return;
    }

    // Copied rather than referenced: the engine reads the file for the whole session, and the place
    // it was picked from may be a removable disk or a portal mount that will not stay put.
    //
    // Staged and then moved into place, so an interrupted copy cannot leave a half-written file
    // that looks installed.
    g_autoptr(GFile) destination = rom_destination();
    g_autofree char* staging_path = g_strconcat(g_file_peek_path(destination), ".importing", nullptr);
    g_autoptr(GFile) staging = g_file_new_for_path(staging_path);

    g_file_delete(staging, nullptr, nullptr);

    if (!g_file_copy(picked, staging, G_FILE_COPY_OVERWRITE, nullptr, nullptr, nullptr, &error)) {
        ts_window_report(self, "That file could not be copied", error->message);
        return;
    }

    if (!g_file_move(staging, destination, G_FILE_COPY_OVERWRITE, nullptr, nullptr, nullptr,
                     &error)) {
        g_file_delete(staging, nullptr, nullptr);
        ts_window_report(self, "That file could not be installed", error->message);
        return;
    }

    g_settings_set_boolean(self->settings, "rom-verified", FALSE);
    ts_window_load_rom_async(self, g_file_peek_path(destination), TRUE);
}

static void on_import_rom(GSimpleAction*, GVariant*, gpointer user_data)
{
    auto* self = TS_WINDOW(user_data);

    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Choose SCCore.dll");

    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "SCCore.dll");
    gtk_file_filter_add_pattern(filter, "*.dll");

    g_autoptr(GListStore) filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, filter);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));

    gtk_file_dialog_open(dialog, GTK_WINDOW(self), nullptr, on_rom_chosen, self);
    g_object_unref(dialog);
}

/// Loads an already-imported ROM at startup.
///
/// Two-tier verification: a file that has passed a full hash once needs only its size and PE
/// timestamp checked on later launches, which is the difference between an instant start and a
/// second of hashing every time.
static void ts_window_load_stored_rom(TsWindow* self)
{
    g_autoptr(GFile) destination = rom_destination();
    if (!g_file_query_exists(destination, nullptr)) {
        return;
    }

    const gboolean verified = g_settings_get_boolean(self->settings, "rom-verified");
    ts_window_load_rom_async(self, g_file_peek_path(destination), !verified);
}

// -- Export --------------------------------------------------------------------------------------

static void on_export_finished(GObject* source, GAsyncResult* result, gpointer user_data)
{
    auto* self = TS_WINDOW(user_data);

    g_autoptr(GError) error = nullptr;
    const gboolean ok =
        ts_player_model_export_wav_finish(TS_PLAYER_MODEL(source), result, &error);

    g_clear_object(&self->mpris);
    g_clear_object(&self->export_cancellable);

    if (!ok && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        ts_window_report(self, "That song could not be exported", error->message);
    }
}

static void on_export_chosen(GObject* source, GAsyncResult* result, gpointer user_data)
{
    auto* self = TS_WINDOW(user_data);

    g_autoptr(GError) error = nullptr;
    g_autoptr(GFile) file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), result, &error);
    if (file == nullptr) {
        return;
    }

    g_autofree char* path = g_file_get_path(file);
    if (path == nullptr) {
        ts_window_report(self, "That location cannot be written",
                         "Choose somewhere on a local filesystem.");
        return;
    }

    g_clear_object(&self->mpris);
    g_clear_object(&self->export_cancellable);
    self->export_cancellable = g_cancellable_new();

    ts_player_model_export_wav(self->model, path, self->export_cancellable, on_export_finished,
                               self);
}

static void on_export(GSimpleAction*, GVariant*, gpointer user_data)
{
    auto* self = TS_WINDOW(user_data);

    g_autofree char* song = nullptr;
    g_object_get(self->model, "song-name", &song, nullptr);
    if (song == nullptr) {
        return;
    }

    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Export WAV");

    // The song's name with its extension swapped, which is almost always what is wanted.
    g_autofree char* stem = g_strdup(song);
    if (char* dot = g_strrstr(stem, ".")) {
        *dot = '\0';
    }
    g_autofree char* suggested = g_strconcat(stem, ".wav", nullptr);
    gtk_file_dialog_set_initial_name(dialog, suggested);

    gtk_file_dialog_save(dialog, GTK_WINDOW(self), nullptr, on_export_chosen, self);
    g_object_unref(dialog);
}

// -- Transport actions ---------------------------------------------------------------------------

static void on_cancel_export(GSimpleAction*, GVariant*, gpointer user_data)
{
    auto* self = TS_WINDOW(user_data);
    if (self->export_cancellable != nullptr) {
        g_cancellable_cancel(self->export_cancellable);
    }
}

static void on_play_pause(GSimpleAction*, GVariant*, gpointer user_data)
{
    ts_player_model_toggle_playing(TS_WINDOW(user_data)->model);
}

static void on_rewind(GSimpleAction*, GVariant*, gpointer user_data)
{
    ts_player_model_seek(TS_WINDOW(user_data)->model, 0.0);
}

static void on_panic(GSimpleAction*, GVariant*, gpointer user_data)
{
    ts_player_model_panic(TS_WINDOW(user_data)->model);
}

void ts_window_present_preferences(TsWindow* self)
{
    adw_dialog_present(ts_prefs_dialog_new(self->model), GTK_WIDGET(self));
}

// -- Construction --------------------------------------------------------------------------------

static void ts_window_dispose(GObject* object)
{
    auto* self = TS_WINDOW(object);

    g_clear_object(&self->mpris);
    g_clear_object(&self->export_cancellable);
    g_clear_object(&self->pending);
    g_clear_object(&self->model);

    gtk_widget_dispose_template(GTK_WIDGET(object), TS_TYPE_WINDOW);
    G_OBJECT_CLASS(ts_window_parent_class)->dispose(object);
}

static void ts_window_class_init(TsWindowClass* klass)
{
    G_OBJECT_CLASS(klass)->dispose = ts_window_dispose;

    GtkWidgetClass* widget_class = GTK_WIDGET_CLASS(klass);

    // Referenced from the template, so the type has to exist before it is parsed.
    g_type_ensure(TS_TYPE_ROM_SETUP);

    gtk_widget_class_set_template_from_resource(
        widget_class, "/co/losno/TabulaSonoraPlayer/ui/window.ui");

    gtk_widget_class_bind_template_child(widget_class, TsWindow, screens);
    gtk_widget_class_bind_template_child(widget_class, TsWindow, rom_setup);
    gtk_widget_class_bind_template_child(widget_class, TsWindow, window_title);
    gtk_widget_class_bind_template_child(widget_class, TsWindow, layouts);
    gtk_widget_class_bind_template_child(widget_class, TsWindow, recent_section);
}

static void ts_window_init(TsWindow* self)
{
    gtk_widget_init_template(GTK_WIDGET(self));

    self->settings = ts_settings_get();
    self->model = ts_player_model_new();

    static const GActionEntry entries[] = {
        {"open", on_open, nullptr, nullptr, nullptr, {0, 0, 0}},
        {"import-rom", on_import_rom, nullptr, nullptr, nullptr, {0, 0, 0}},
        {"export", on_export, nullptr, nullptr, nullptr, {0, 0, 0}},
        {"cancel-export", on_cancel_export, nullptr, nullptr, nullptr, {0, 0, 0}},
        {"play-pause", on_play_pause, nullptr, nullptr, nullptr, {0, 0, 0}},
        {"rewind", on_rewind, nullptr, nullptr, nullptr, {0, 0, 0}},
        {"panic", on_panic, nullptr, nullptr, nullptr, {0, 0, 0}},
        {"open-recent", on_open_recent, "s", nullptr, nullptr, {0, 0, 0}},
    };
    g_action_map_add_action_entries(G_ACTION_MAP(self), entries, G_N_ELEMENTS(entries), self);

    g_signal_connect_object(gtk_recent_manager_get_default(), "changed",
                            G_CALLBACK(ts_window_refresh_recent), self, G_CONNECT_SWAPPED);
    ts_window_refresh_recent(self);

    self->transport = ts_transport_new(self->model);
    self->mixer = ts_mixer_new(self->model);
    adw_multi_layout_view_set_child(self->layouts, "transport", self->transport);
    adw_multi_layout_view_set_child(self->layouts, "mixer", self->mixer);

    GtkDropTarget* drop = gtk_drop_target_new(G_TYPE_FILE, GDK_ACTION_COPY);
    g_signal_connect(drop, "drop", G_CALLBACK(on_drop), self);
    gtk_widget_add_controller(GTK_WIDGET(self), GTK_EVENT_CONTROLLER(drop));

    g_settings_bind(self->settings, "window-width", self, "default-width", G_SETTINGS_BIND_DEFAULT);
    g_settings_bind(self->settings, "window-height", self, "default-height", G_SETTINGS_BIND_DEFAULT);
    g_settings_bind(self->settings, "window-maximized", self, "maximized", G_SETTINGS_BIND_DEFAULT);

    ts_settings_bind_model(self->settings, self->model);

    g_signal_connect_object(self->model, "notify::rom-name", G_CALLBACK(on_model_notify), self,
                            G_CONNECT_DEFAULT);
    g_signal_connect_object(self->model, "notify::song-name", G_CALLBACK(on_model_notify), self,
                            G_CONNECT_DEFAULT);

    self->mpris = ts_mpris_new(self->model, GTK_WINDOW(self));

    ts_window_update_screen(self);
    ts_window_load_stored_rom(self);
}

TsWindow* ts_window_new(AdwApplication* application)
{
    return TS_WINDOW(g_object_new(TS_TYPE_WINDOW, "application", application, nullptr));
}

#include "app/ts-application.hpp"

#include "app/ts-window.hpp"

struct _TsApplication {
    AdwApplication parent_instance;
};

G_DEFINE_FINAL_TYPE(TsApplication, ts_application, ADW_TYPE_APPLICATION)

static TsWindow* ts_application_window(GApplication* application)
{
    GtkWindow* window = gtk_application_get_active_window(GTK_APPLICATION(application));

    if (window == nullptr) {
        window = GTK_WINDOW(ts_window_new(ADW_APPLICATION(application)));
    }

    return TS_WINDOW(window);
}

static void ts_application_activate(GApplication* application)
{
    gtk_window_present(GTK_WINDOW(ts_application_window(application)));
}

/// Files from the command line, from a file manager, or from `open`.
///
/// Required rather than optional: the application declares G_APPLICATION_HANDLES_OPEN so that the
/// desktop entry's `%f` works, and GApplication refuses to start if that is claimed without being
/// implemented.
static void ts_application_open(GApplication* application, GFile** files, int n_files,
                                const char* hint)
{
    (void)hint;

    TsWindow* window = ts_application_window(application);
    gtk_window_present(GTK_WINDOW(window));

    // No playlist, so the last one named wins -- the same as opening them one after another.
    if (n_files > 0) {
        ts_window_open_file(window, files[n_files - 1]);
    }
}

static void on_quit(GSimpleAction* action, GVariant* parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    g_application_quit(G_APPLICATION(user_data));
}

static void on_preferences(GSimpleAction* action, GVariant* parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;

    GtkWindow* window = gtk_application_get_active_window(GTK_APPLICATION(user_data));
    if (window != nullptr) {
        ts_window_present_preferences(TS_WINDOW(window));
    }
}

static void on_about(GSimpleAction* action, GVariant* parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;

    auto* application = GTK_APPLICATION(user_data);
    AdwDialog* about = adw_about_dialog_new();

    adw_about_dialog_set_application_name(ADW_ABOUT_DIALOG(about), "Tabula Sonora");
    adw_about_dialog_set_application_icon(ADW_ABOUT_DIALOG(about), TSGUI_APP_ID);
    adw_about_dialog_set_version(ADW_ABOUT_DIALOG(about), TSGUI_VERSION);
    adw_about_dialog_set_license_type(ADW_ABOUT_DIALOG(about), GTK_LICENSE_BSD_3);
    adw_about_dialog_set_comments(
        ADW_ABOUT_DIALOG(about),
        "Plays MIDI files through a native reimplementation of the Roland Sound Canvas VA voice.");

    // Not decoration: reverb and chorus coefficients compiled into the engine are Roland-derived,
    // and the upstream notice has to travel with any binary that carries them.
    adw_about_dialog_add_legal_section(
        ADW_ABOUT_DIALOG(about), "Sound Canvas voice", nullptr, GTK_LICENSE_UNKNOWN,
        "Reimplemented from a licensed copy of SCCore.dll, which is read as data and never loaded "
        "as code. Reverb and chorus coefficients are Roland-derived; see the NOTICE file "
        "distributed with this program.");

    adw_dialog_present(about, GTK_WIDGET(gtk_application_get_active_window(application)));
}

static void ts_application_startup(GApplication* application)
{
    G_APPLICATION_CLASS(ts_application_parent_class)->startup(application);

    // Our own marks, from the binary rather than from an installed icon theme, so they resolve in a
    // development run as well as an installed one.
    gtk_icon_theme_add_resource_path(
        gtk_icon_theme_get_for_display(gdk_display_get_default()),
        "/co/losno/TabulaSonoraPlayer/icons");

    static const GActionEntry entries[] = {
        {"quit", on_quit, nullptr, nullptr, nullptr, {0, 0, 0}},
        {"about", on_about, nullptr, nullptr, nullptr, {0, 0, 0}},
        {"preferences", on_preferences, nullptr, nullptr, nullptr, {0, 0, 0}},
    };

    g_action_map_add_action_entries(G_ACTION_MAP(application), entries, G_N_ELEMENTS(entries),
                                    application);

    // Space with no modifier, as on the transport button itself, because that is what a transport
    // is expected to answer to.
    static const struct {
        const char* action;
        const char* accels[3];
    } bindings[] = {
        {"app.quit", {"<Control>q", nullptr, nullptr}},
        {"app.preferences", {"<Control>comma", nullptr, nullptr}},
        {"win.open", {"<Control>o", nullptr, nullptr}},
        {"win.export", {"<Control>e", nullptr, nullptr}},
        {"win.play-pause", {"space", nullptr, nullptr}},
        {"win.panic", {"<Control>period", nullptr, nullptr}},
        {"win.rewind", {"<Control>Home", nullptr, nullptr}},
    };

    for (const auto& binding : bindings) {
        gtk_application_set_accels_for_action(GTK_APPLICATION(application), binding.action,
                                              binding.accels);
    }
}

static void ts_application_class_init(TsApplicationClass* klass)
{
    GApplicationClass* application_class = G_APPLICATION_CLASS(klass);

    application_class->activate = ts_application_activate;
    application_class->startup = ts_application_startup;
    application_class->open = ts_application_open;
}

static void ts_application_init(TsApplication* self)
{
    (void)self;
}

TsApplication* ts_application_new(void)
{
    return TS_APPLICATION(g_object_new(TS_TYPE_APPLICATION,
                                       "application-id", TSGUI_APP_ID,
                                       "flags", G_APPLICATION_HANDLES_OPEN,
                                       nullptr));
}

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

    // The organisation rather than this repository, because the thing a reader is looking for is
    // rarely only this one: the player, the engine it renders through and the SwiftUI port it was
    // ported from are three repositories under here, and which of them a question belongs to is
    // usually clearer from the outside than from in this dialog. The engine's own link below is the
    // exception, and it is exact because it names a commit.
    adw_about_dialog_set_website(ADW_ABOUT_DIALOG(about), "https://github.com/TabulaSonora");

    // Not decoration: reverb and chorus coefficients compiled into the engine are Roland-derived,
    // and the upstream notice has to travel with any binary that carries them.
    // GTK_LICENSE_CUSTOM, not GTK_LICENSE_UNKNOWN: the custom text is only rendered for the custom
    // type, and with UNKNOWN the section shows its heading and nothing under it.
    adw_about_dialog_add_legal_section(
        ADW_ABOUT_DIALOG(about), "Sound Canvas Voice", nullptr, GTK_LICENSE_CUSTOM,
        "Reimplemented from a licensed copy of SCCore.dll, which is read as data and never loaded "
        "as code. Reverb and chorus coefficients are Roland-derived; see the NOTICE file "
        "distributed with this program.");

    // The engine's commit, on the Details page beside the version.
    //
    // The version above names the player; this names the voice, and they move independently. A
    // report that a kit sounds wrong is about a NativeTS commit, not about "0.1.0", and the person
    // making it should not have to be told how to find out which one they are running.
    //
    // The link goes to the commit rather than to the repository root because that is what makes the
    // hash worth showing: it lands on the change itself. Seven characters in the title, as git
    // abbreviates them, with the whole hash carried in the URL for anyone who needs it exact.
    if (TSGUI_NATIVETS_COMMIT[0] != '\0') {
        char* title = g_strdup_printf("Sound Canvas Voice Engine (%.7s)", TSGUI_NATIVETS_COMMIT);
        char* url = g_strdup_printf("https://github.com/TabulaSonora/NativeTS/commit/%s",
                                    TSGUI_NATIVETS_COMMIT);

        adw_about_dialog_add_link(ADW_ABOUT_DIALOG(about), title, url);

        g_free(title);
        g_free(url);
    } else {
        // Built from a tarball, or from an installed engine: there is no commit to name, and a row
        // reading "unknown" would be worse than one that simply goes to the project.
        adw_about_dialog_add_link(ADW_ABOUT_DIALOG(about), "Sound Canvas Voice Engine",
                                  "https://github.com/TabulaSonora/NativeTS");
    }

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

    // Our own metrics over the platform stylesheet. APPLICATION priority rather than USER: this is
    // the program stating how its own widgets are proportioned, and a user stylesheet is still
    // entitled to disagree with it.
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(css, "/co/losno/TabulaSonoraPlayer/ui/style.css");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                               GTK_STYLE_PROVIDER(css),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

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
        {"win.song-info", {"<Control>i", nullptr, nullptr}},
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

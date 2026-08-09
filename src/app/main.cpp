#include "app/ts-application.hpp"

#include <glib/gi18n.h>

#include <clocale>

int main(int argc, char* argv[])
{
    // GTK runs setlocale(LC_ALL, "") itself inside gtk_init, and will overwrite whatever is set
    // here -- so this is not the call that chooses the locale, and trying to force one
    // programmatically before g_application_run does not work. It is here for the window between
    // process start and gtk_init, where anything locale-sensitive would otherwise run under "C".
    std::setlocale(LC_ALL, "");

    // An uninstalled build has no catalogue under the install prefix, which would leave a developer
    // run untranslated with no indication why. The override is the same shape as the
    // GSETTINGS_SCHEMA_DIR one an uninstalled run already needs:
    //
    //   TS_LOCALE_DIR=build/dev/po/locale GSETTINGS_SCHEMA_DIR=build/dev/data
    //       ./build/dev/src/tabula-sonora-player
    const char* locale_dir = g_getenv("TS_LOCALE_DIR");
    bindtextdomain(GETTEXT_PACKAGE, (locale_dir && *locale_dir) ? locale_dir : TSGUI_LOCALE_DIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");

    // Setting the default domain, not just binding ours, is what makes the window.ui template
    // translate. GtkBuilder looks a template's strings up with g_dgettext(NULL, ...) -- a class
    // template has no translation domain of its own and GTK 4 offers no way to give it one -- and a
    // null domain means the process default.
    textdomain(GETTEXT_PACKAGE);

    g_set_application_name(_("Tabula Sonora"));

    g_autoptr(TsApplication) application = ts_application_new();
    return g_application_run(G_APPLICATION(application), argc, argv);
}

#include "app/ts-application.hpp"

#include <glib/gi18n.h>

int main(int argc, char* argv[])
{
    g_set_application_name("Tabula Sonora");

    g_autoptr(TsApplication) application = ts_application_new();
    return g_application_run(G_APPLICATION(application), argc, argv);
}

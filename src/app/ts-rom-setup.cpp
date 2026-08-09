#include "app/ts-rom-setup.hpp"

#include "tabulasonora/table_manifest.hpp"

#include <glib/gi18n.h>

struct _TsRomSetup {
    GtkWidget parent_instance;

    GtkWidget* stack;
};

G_DEFINE_FINAL_TYPE(TsRomSetup, ts_rom_setup, GTK_TYPE_WIDGET)

namespace {

/// A read-only row whose value can be selected and copied, which matters for a SHA-256 that
/// somebody is going to want to paste into a checksum command.
AdwActionRow* identity_row(const char* title, const char* value, bool monospace)
{
    auto* row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);

    GtkWidget* label = gtk_label_new(value);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_widget_add_css_class(label, "dim-label");
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);

    if (monospace) {
        gtk_widget_add_css_class(label, "monospace");
        gtk_widget_add_css_class(label, "caption");
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
        gtk_label_set_max_width_chars(GTK_LABEL(label), 24);
    }

    adw_action_row_add_suffix(row, label);
    return row;
}

} // namespace

static void ts_rom_setup_dispose(GObject* object)
{
    if (GtkWidget* child = gtk_widget_get_first_child(GTK_WIDGET(object))) {
        gtk_widget_unparent(child);
    }
    G_OBJECT_CLASS(ts_rom_setup_parent_class)->dispose(object);
}

static void ts_rom_setup_class_init(TsRomSetupClass* klass)
{
    G_OBJECT_CLASS(klass)->dispose = ts_rom_setup_dispose;
    gtk_widget_class_set_layout_manager_type(GTK_WIDGET_CLASS(klass), GTK_TYPE_BIN_LAYOUT);
    gtk_widget_class_set_css_name(GTK_WIDGET_CLASS(klass), "romsetup");
}

static void ts_rom_setup_init(TsRomSetup* self)
{
    // Straight from the compiled-in manifest, so it can never drift from what the engine will
    // actually accept.
    const ts::DllIdentity& pinned = ts::TableManifest::defaults().dll();

    auto* page = ADW_STATUS_PAGE(adw_status_page_new());
    adw_status_page_set_icon_name(page, "co.losno.TabulaSonoraPlayer-symbolic");
    adw_status_page_set_title(page, _("Choose Your Sound Canvas ROM"));

    /* TRANSLATORS: the two %s are the product name and version of the Roland software the DLL
       comes from, e.g. "SOUND Canvas VA" and "1.1.6". */
    g_autofree char* description = g_strdup_printf(
        _("Tabula Sonora plays through the Roland Sound Canvas voice, which lives inside "
          "SCCore.dll. Point it at your own copy from %s %s."),
        pinned.product.c_str(), pinned.version.c_str());
    adw_status_page_set_description(page, description);

    GtkWidget* body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_set_halign(body, GTK_ALIGN_CENTER);

    // Button and spinner occupy the same slot, so the layout does not jump while the file is being
    // hashed.
    self->stack = gtk_stack_new();
    gtk_widget_set_halign(self->stack, GTK_ALIGN_CENTER);

    GtkWidget* choose = gtk_button_new_with_label(_("Choose SCCore.dll…"));
    gtk_widget_add_css_class(choose, "suggested-action");
    gtk_widget_add_css_class(choose, "pill");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(choose), "win.import-rom");
    gtk_stack_add_named(GTK_STACK(self->stack), choose, "choose");

    GtkWidget* checking = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(checking, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(checking), adw_spinner_new());
    gtk_box_append(GTK_BOX(checking), gtk_label_new(_("Checking the file…")));
    gtk_stack_add_named(GTK_STACK(self->stack), checking, "checking");

    gtk_box_append(GTK_BOX(body), self->stack);

    auto* group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(group, _("The File It Needs"));

    // A natural width rather than a floor of its own: the SHA-256 row already ellipsizes, and a
    // hard minimum here would set the whole window's minimum width.
    gtk_widget_set_hexpand(GTK_WIDGET(group), TRUE);

    g_autofree char* size = g_format_size(static_cast<guint64>(pinned.size));
    adw_preferences_group_add(group,
                              GTK_WIDGET(identity_row(_("File"), pinned.file_name.c_str(), false)));
    adw_preferences_group_add(group, GTK_WIDGET(identity_row(_("Size"), size, false)));
    adw_preferences_group_add(group,
                              GTK_WIDGET(identity_row(_("SHA-256"), pinned.sha256.c_str(), true)));

    gtk_box_append(GTK_BOX(body), GTK_WIDGET(group));

    adw_status_page_set_child(page, body);
    gtk_widget_set_parent(GTK_WIDGET(page), GTK_WIDGET(self));
}

GtkWidget* ts_rom_setup_new(void)
{
    return GTK_WIDGET(g_object_new(TS_TYPE_ROM_SETUP, nullptr));
}

void ts_rom_setup_set_verifying(TsRomSetup* self, gboolean verifying)
{
    gtk_stack_set_visible_child_name(GTK_STACK(self->stack), verifying ? "checking" : "choose");
}

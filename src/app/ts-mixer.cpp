#include "app/ts-mixer.hpp"

#include "app/ts-part-object.hpp"
#include "app/ts-part-row.hpp"

struct _TsMixer {
    GtkWidget parent_instance;

    TsPlayerModel* model;

    GtkWidget* stack;
    GtkWidget* list;

    GtkFilter* filter;
    GtkFilterListModel* visible;

    GtkSorter* sorter;
    GtkSortListModel* ordered;
};

G_DEFINE_FINAL_TYPE(TsMixer, ts_mixer, GTK_TYPE_WIDGET)

// -- Which parts to show -------------------------------------------------------------------------

static gboolean part_is_present(gpointer item, gpointer user_data)
{
    (void)user_data;
    return ts_part_get_present(TS_PART(item));
}

static void on_presence_changed(TsMixer* self)
{
    gtk_filter_changed(self->filter, GTK_FILTER_CHANGE_DIFFERENT);
}

// -- What order to show them in ------------------------------------------------------------------

/// In the order the strips are *labelled*, which is the receive channel and not the slot.
///
/// Listing by slot while labelling by channel is worse than either alone: a file that moves a part
/// shows 10, 1, 2, 3 down the left edge, which reads as broken numbering rather than as
/// information. Port first, so a multi-port score still groups the way it is labelled, and the slot
/// breaks ties -- GS allows two parts pointed at one channel and both strips have to appear.
static int compare_parts(gconstpointer left, gconstpointer right, gpointer user_data)
{
    (void)user_data;

    auto* a = TS_PART(const_cast<gpointer>(left));
    auto* b = TS_PART(const_cast<gpointer>(right));

    if (ts_part_get_port(a) != ts_part_get_port(b)) {
        return ts_part_get_port(a) < ts_part_get_port(b) ? -1 : 1;
    }
    if (ts_part_get_channel(a) != ts_part_get_channel(b)) {
        return ts_part_get_channel(a) < ts_part_get_channel(b) ? -1 : 1;
    }
    if (ts_part_get_index(a) != ts_part_get_index(b)) {
        return ts_part_get_index(a) < ts_part_get_index(b) ? -1 : 1;
    }
    return 0;
}

static void on_routing_changed(TsMixer* self)
{
    gtk_sorter_changed(self->sorter, GTK_SORTER_CHANGE_DIFFERENT);
}

static void on_visible_changed(GListModel* model, guint position, guint removed, guint added,
                               gpointer user_data)
{
    (void)position;
    (void)removed;
    (void)added;

    auto* self = TS_MIXER(user_data);
    const gboolean any = g_list_model_get_n_items(model) > 0;
    gtk_stack_set_visible_child_name(GTK_STACK(self->stack), any ? "list" : "empty");
}

// -- The list view factory -----------------------------------------------------------------------

static void on_setup(GtkSignalListItemFactory* factory, GtkListItem* item, gpointer user_data)
{
    (void)factory;
    gtk_list_item_set_child(item, ts_part_row_new(TS_MIXER(user_data)->model));
    gtk_list_item_set_activatable(item, FALSE);
}

static void on_bind(GtkSignalListItemFactory* factory, GtkListItem* item, gpointer user_data)
{
    (void)factory;
    (void)user_data;
    ts_part_row_bind(TS_PART_ROW(gtk_list_item_get_child(item)),
                     TS_PART(gtk_list_item_get_item(item)));
}

static void on_unbind(GtkSignalListItemFactory* factory, GtkListItem* item, gpointer user_data)
{
    (void)factory;
    (void)user_data;
    ts_part_row_unbind(TS_PART_ROW(gtk_list_item_get_child(item)));
}

// -- Construction --------------------------------------------------------------------------------

static void ts_mixer_dispose(GObject* object)
{
    auto* self = TS_MIXER(object);

    if (GtkWidget* child = gtk_widget_get_first_child(GTK_WIDGET(self))) {
        gtk_widget_unparent(child);
    }
    g_clear_object(&self->ordered);
    g_clear_object(&self->visible);

    G_OBJECT_CLASS(ts_mixer_parent_class)->dispose(object);
}

static void ts_mixer_class_init(TsMixerClass* klass)
{
    G_OBJECT_CLASS(klass)->dispose = ts_mixer_dispose;

    gtk_widget_class_set_layout_manager_type(GTK_WIDGET_CLASS(klass), GTK_TYPE_BIN_LAYOUT);
    gtk_widget_class_set_css_name(GTK_WIDGET_CLASS(klass), "mixer");
}

static void ts_mixer_init(TsMixer* self)
{
    // Explicitly, rather than relying on the flag propagating up from the scroller inside: this
    // widget sits in an AdwLayoutSlot, and the slot is what the surrounding box actually measures.
    gtk_widget_set_vexpand(GTK_WIDGET(self), TRUE);

    self->stack = gtk_stack_new();
    gtk_widget_set_vexpand(self->stack, TRUE);
    gtk_widget_set_parent(self->stack, GTK_WIDGET(self));

    AdwStatusPage* empty = ADW_STATUS_PAGE(adw_status_page_new());
    adw_status_page_set_icon_name(empty, "audio-x-generic-symbolic");
    adw_status_page_set_title(empty, "No Parts");
    adw_status_page_set_description(empty, "Open a MIDI file to see the parts it addresses.");
    gtk_stack_add_named(GTK_STACK(self->stack), GTK_WIDGET(empty), "empty");
}

GtkWidget* ts_mixer_new(TsPlayerModel* model)
{
    auto* self = TS_MIXER(g_object_new(TS_TYPE_MIXER, nullptr));
    self->model = model;

    self->filter = GTK_FILTER(gtk_custom_filter_new(part_is_present, nullptr, nullptr));
    self->visible = gtk_filter_list_model_new(
        G_LIST_MODEL(g_object_ref(ts_player_model_get_parts(model))), self->filter);

    gtk_filter_list_model_set_incremental(self->visible, FALSE);

    // Sorted over the filtered set rather than the whole store, so the comparison runs over the
    // handful of parts a file addresses instead of all sixty-four.
    self->sorter = GTK_SORTER(gtk_custom_sorter_new(compare_parts, nullptr, nullptr));
    self->ordered =
        gtk_sort_list_model_new(G_LIST_MODEL(g_object_ref(self->visible)), self->sorter);
    gtk_sort_list_model_set_incremental(self->ordered, FALSE);

    auto* factory = GTK_LIST_ITEM_FACTORY(gtk_signal_list_item_factory_new());
    g_signal_connect(factory, "setup", G_CALLBACK(on_setup), self);
    g_signal_connect(factory, "bind", G_CALLBACK(on_bind), self);
    g_signal_connect(factory, "unbind", G_CALLBACK(on_unbind), self);

    self->list = gtk_list_view_new(
        GTK_SELECTION_MODEL(gtk_no_selection_new(G_LIST_MODEL(g_object_ref(self->ordered)))),
        factory);
    gtk_list_view_set_show_separators(GTK_LIST_VIEW(self->list), TRUE);
    gtk_widget_add_css_class(self->list, "navigation-sidebar");

    GtkWidget* scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), self->list);
    gtk_widget_set_vexpand(scroller, TRUE);

    gtk_stack_add_named(GTK_STACK(self->stack), scroller, "list");

    g_signal_connect(self->visible, "items-changed", G_CALLBACK(on_visible_changed), self);
    on_visible_changed(G_LIST_MODEL(self->visible), 0, 0, 0, self);

    // The store never changes length -- only the parts' `present` flags do -- and a filter list
    // model does not watch its items' properties. The model tells us when the set actually differs.
    g_signal_connect_object(model, "presence-changed", G_CALLBACK(on_presence_changed), self,
                            G_CONNECT_SWAPPED);

    // Likewise for the order: a sorter watches its items' properties no more closely than a filter
    // does, and a part can be moved to another channel without the addressed set changing at all.
    g_signal_connect_object(model, "routing-changed", G_CALLBACK(on_routing_changed), self,
                            G_CONNECT_SWAPPED);

    return GTK_WIDGET(self);
}

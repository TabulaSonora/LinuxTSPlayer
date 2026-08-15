#include "app/ts-prefs-dialog.hpp"

#include "app/ts-settings.hpp"
#include "app/ts-tone-map.hpp"

#include <glib/gi18n.h>

#include <vector>

struct _TsPrefsDialog {
    AdwPreferencesDialog parent_instance;

    TsPlayerModel* model;
    GSettings* settings;

    GtkWidget* buffer_note;
};

G_DEFINE_FINAL_TYPE(TsPrefsDialog, ts_prefs_dialog, ADW_TYPE_PREFERENCES_DIALOG)

namespace {

/// An AdwComboRow over a fixed set of values, bound to an integer key.
///
/// The row's own "selected" property is an index into its model, which is not the value we want to
/// store -- the tone map's numbers are the module's own bank codes and polyphony is a voice count.
/// So the mapping is explicit in both directions.
struct ChoiceRow {
    GSettings* settings;
    const char* key;
    std::vector<int> values;

    /// Set while writing the row from the key, so the row's own handler does not write the key
    /// straight back and fight whoever else is holding it.
    bool updating = false;
};

/// Hung off the row so both handlers can reach it and it dies with the row, rather than being owned
/// by whichever signal connection happens to be torn down last.
ChoiceRow* choice_of(gpointer row)
{
    return static_cast<ChoiceRow*>(g_object_get_data(G_OBJECT(row), "ts-choice"));
}

void on_choice_selected(GObject* row, GParamSpec*, gpointer)
{
    auto* choice = choice_of(row);
    if (choice->updating) {
        return;
    }

    const guint index = adw_combo_row_get_selected(ADW_COMBO_ROW(row));
    if (index < choice->values.size()) {
        g_settings_set_int(choice->settings, choice->key, choice->values[index]);
    }
}

void on_choice_key_changed(GSettings* settings, const char* key, gpointer row)
{
    auto* choice = choice_of(row);
    const int value = g_settings_get_int(settings, key);

    for (std::size_t i = 0; i < choice->values.size(); ++i) {
        if (choice->values[i] == value) {
            choice->updating = true;
            adw_combo_row_set_selected(ADW_COMBO_ROW(row), static_cast<guint>(i));
            choice->updating = false;
            return;
        }
    }
}

AdwComboRow* add_choice(AdwPreferencesGroup* group, GSettings* settings, const char* key,
                        const char* title, const char* subtitle,
                        const std::vector<std::pair<const char*, int>>& options)
{
    auto* row = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    if (subtitle != nullptr) {
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
    }

    auto* choice = new ChoiceRow{settings, key, {}, false};

    GtkStringList* labels = gtk_string_list_new(nullptr);
    for (const auto& [label, value] : options) {
        gtk_string_list_append(labels, label);
        choice->values.push_back(value);
    }
    adw_combo_row_set_model(row, G_LIST_MODEL(labels));

    g_object_set_data_full(G_OBJECT(row), "ts-choice", choice,
                           [](gpointer data) { delete static_cast<ChoiceRow*>(data); });

    on_choice_key_changed(settings, key, row);

    g_signal_connect(row, "notify::selected", G_CALLBACK(on_choice_selected), nullptr);

    g_autofree char* detailed = g_strdup_printf("changed::%s", key);
    g_signal_connect_object(settings, detailed, G_CALLBACK(on_choice_key_changed), row,
                            G_CONNECT_DEFAULT);

    adw_preferences_group_add(group, GTK_WIDGET(row));
    return row;
}

AdwSwitchRow* add_switch(AdwPreferencesGroup* group, GSettings* settings, const char* key,
                         const char* title, const char* subtitle)
{
    auto* row = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    if (subtitle != nullptr) {
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
    }
    g_settings_bind(settings, key, row, "active", G_SETTINGS_BIND_DEFAULT);
    adw_preferences_group_add(group, GTK_WIDGET(row));
    return row;
}

} // namespace

// -- Latency -------------------------------------------------------------------------------------

static void ts_prefs_dialog_update_buffer_note(TsPrefsDialog* self)
{
    gint64 underruns = 0;
    g_object_get(self->model, "underruns", &underruns, nullptr);

    if (underruns > 0) {
        // %ld and a cast rather than G_GINT64_FORMAT: xgettext concatenates adjacent string
        // literals but does not expand macros, so a msgid spliced together around one extracts as
        // bare "%" -- silently, with no warning -- and the translation is never found. A dropout
        // counter cannot approach the range where long and gint64 differ on any target we build
        // for.
        g_autofree char* text = g_strdup_printf(
            g_dngettext(nullptr, "%ld dropout so far", "%ld dropouts so far",
                        static_cast<gulong>(underruns)),
            static_cast<long>(underruns));
        gtk_label_set_text(GTK_LABEL(self->buffer_note), text);
        gtk_widget_remove_css_class(self->buffer_note, "dim-label");
        gtk_widget_add_css_class(self->buffer_note, "warning");
    } else {
        gtk_label_set_text(GTK_LABEL(self->buffer_note), _("No dropouts"));
        gtk_widget_remove_css_class(self->buffer_note, "warning");
        gtk_widget_add_css_class(self->buffer_note, "dim-label");
    }
}

static void on_underruns_notify(GObject*, GParamSpec*, gpointer user_data)
{
    ts_prefs_dialog_update_buffer_note(TS_PREFS_DIALOG(user_data));
}

// -- Construction --------------------------------------------------------------------------------

static void ts_prefs_dialog_class_init(TsPrefsDialogClass*) {}

static void ts_prefs_dialog_init(TsPrefsDialog*) {}

AdwDialog* ts_prefs_dialog_new(TsPlayerModel* model)
{
    auto* self = TS_PREFS_DIALOG(g_object_new(TS_TYPE_PREFS_DIALOG, nullptr));
    self->model = model;
    self->settings = ts_settings_get();

    adw_dialog_set_title(ADW_DIALOG(self), _("Preferences"));

    auto* page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(page, _("Engine"));
    adw_preferences_page_set_icon_name(page, "applications-engineering-symbolic");

    // -- Voice --
    auto* voice = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(voice, _("Voice"));

    // Straight from the library, so a vintage added upstream appears here without being listed
    // twice.
    std::vector<std::pair<const char*, int>> maps;
    for (const auto& [name, value] : ts::tone_map_choices()) {
        (void)name;
        maps.emplace_back(ts_tone_map_display_name(value), value);
    }
    add_choice(voice, self->settings, "map", _("Module"),
               _("Which module's tone map program changes resolve against"), maps);

    // Three separate msgids rather than one with a count: they are fixed labels, not a plural
    // over a runtime number, and a language that inflects "port" differently at 2 and at 4 can say
    // so here without a plural form to carry it.
    add_choice(voice, self->settings, "ports", _("Parts"), nullptr,
               {{_("16 (1 port)"), 1}, {_("32 (2 ports)"), 2}, {_("64 (4 ports)"), 4}});

    add_choice(voice, self->settings, "polyphony", _("Polyphony"), nullptr,
               {{_("64 (hardware)"), 64}, {"128", 128}, {"256", 256}});

    // The one setting here that is not a choice between two things the module does: it is a choice
    // between the module and the machine the module models. Worded from the side that is off by
    // default, because "on" is simply the engine behaving well and needs no explaining.
    add_switch(voice, self->settings, "extended-interpolation", _("Extended Interpolation"),
               _("A wide band-limiting resampler with no pitch ceiling. Turn it off to reproduce "
                 "SCCore.dll exactly, including its aliasing and the glides it stalls."));

    // The other departure from the module, and worded from the other side: this one is off by
    // default, so "on" is the half that needs explaining. Deliberately not phrased as a fix for
    // anything -- a file whose bulk dump the hardware truncates is being played correctly when it
    // is truncated here too, and this offers the other reading of it rather than a better one.
    add_switch(voice, self->settings, "flush-before-sysex", _("Deliver Dropped SysEx"),
               _("The module discards a bulk dump too large for one control tick, and so does "
                 "this. Turn it on to hear such a file as it was written instead."));

    adw_preferences_page_add(page, voice);

    // -- Effects --
    auto* effects = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(effects, _("Effects"));
    add_switch(effects, self->settings, "reverb", _("Reverb"), nullptr);
    add_switch(effects, self->settings, "chorus", _("Chorus"), nullptr);
    add_switch(effects, self->settings, "delay", _("Delay"), nullptr);
    add_switch(effects, self->settings, "efx", _("Insertion Effects"), nullptr);
    adw_preferences_page_add(page, effects);

    // No Output group. Gain used to be one, and now lives in the transport beside the buttons: it
    // is the one engine value that is adjusted while listening rather than decided beforehand, and
    // the only one that costs no rebuild, so nothing about it fits a dialog reached through a menu.

    // -- Latency --
    auto* latency = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(latency, _("Latency"));
    adw_preferences_group_set_description(
        latency, _("How far ahead the engine renders. Lower answers a keyboard sooner; raise it "
                   "if you hear dropouts."));

    auto* buffer_row = ADW_SPIN_ROW(adw_spin_row_new_with_range(10, 400, 5));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(buffer_row), _("Buffer"));
    g_settings_bind(self->settings, "latency-ms", buffer_row, "value", G_SETTINGS_BIND_DEFAULT);

    self->buffer_note = gtk_label_new(_("No dropouts"));
    gtk_widget_add_css_class(self->buffer_note, "caption");
    gtk_widget_add_css_class(self->buffer_note, "dim-label");
    gtk_widget_set_valign(self->buffer_note, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(buffer_row), self->buffer_note);

    adw_preferences_group_add(latency, GTK_WIDGET(buffer_row));
    adw_preferences_page_add(page, latency);

    // -- MIDI --
    auto* midi = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(midi, _("MIDI Input"));
    add_switch(midi, self->settings, "midi-auto-connect", _("Connect Every Source"),
               _("Subscribe to all readable sequencer ports. Leave this off when something else is "
                 "driving the synth, or it will echo back into it."));
    adw_preferences_page_add(page, midi);

    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(self), page);

    g_signal_connect_object(model, "notify::underruns", G_CALLBACK(on_underruns_notify), self,
                            G_CONNECT_DEFAULT);
    ts_prefs_dialog_update_buffer_note(self);

    return ADW_DIALOG(self);
}

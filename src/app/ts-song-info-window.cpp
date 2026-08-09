#include "app/ts-song-info-window.hpp"

#include "app/ts-settings.hpp"
#include "app/ts-tone-map.hpp"
#include "host/ts_session.hpp"

#include <glib/gi18n.h>

#include <cmath>
#include <string>
#include <vector>

struct _TsSongInfoWindow {
    AdwWindow parent_instance;

    /// Unowned, as everywhere else in this interface: the model outlives every window, and an
    /// owning reference the other way would keep the engine alive after the player had closed.
    TsPlayerModel* model;

    AdwViewStack* pages;
    AdwWindowTitle* title;
};

G_DEFINE_FINAL_TYPE(TsSongInfoWindow, ts_song_info_window, ADW_TYPE_WINDOW)

namespace {

using ts::host::SongInfo;
using ts::host::TextEncoding;

// -- Text ------------------------------------------------------------------------------------------

/// Turns a file's bytes into something a label can hold.
///
/// A Standard MIDI File declares no encoding, so `read_song_info` guesses one and this is where the
/// guess is spent. `g_convert` is given the fallback form on purpose: a file that mixes encodings
/// -- and they exist -- should lose the byte it got wrong rather than the whole string, because a
/// track list with one blank row is still a track list and one with seventeen is not.
char* to_utf8(const std::string& raw, TextEncoding encoding)
{
    if (raw.empty()) {
        return g_strdup("");
    }

    const char* charset = ts::host::encoding_name(encoding);
    if (encoding == TextEncoding::ascii || encoding == TextEncoding::utf8) {
        // Already UTF-8 by construction, but only as far as the *sample* that was tested; a string
        // the detector never saw could still be malformed, and GTK aborts on invalid UTF-8.
        if (g_utf8_validate(raw.c_str(), static_cast<gssize>(raw.size()), nullptr)) {
            return g_strdup(raw.c_str());
        }
        charset = "WINDOWS-1252";
    }

    char* converted = g_convert_with_fallback(raw.c_str(), static_cast<gssize>(raw.size()), "UTF-8",
                                              charset, nullptr, nullptr, nullptr, nullptr);
    if (converted != nullptr) {
        return converted;
    }

    // Windows-1252 has no undefined byte that iconv rejects, so this is all but unreachable; a
    // string of question marks still beats an abort.
    return g_strdup("…");
}

/// The same text, escaped for the widgets that parse Pango markup.
///
/// `AdwActionRow`'s title and subtitle do, as do a status page's, and file text is arbitrary: the
/// first real file this was pointed at was credited to "Hoagy Carmichael & Stuart Gorell", whose
/// ampersand is an unterminated entity and makes GTK drop the whole string with a warning. Anything
/// out of a file is escaped on its way into one of those; the lyric sheet is not, because a text
/// buffer takes plain text and would show the escapes.
char* to_markup(const std::string& raw, TextEncoding encoding)
{
    g_autofree char* utf8 = to_utf8(raw, encoding);
    return g_markup_escape_text(utf8, -1);
}

/// Seconds as `m:ss`.
///
/// Truncating rather than rounding, which is what the transport's own `format_time` does. The two
/// read the same `song_length_`, so rounding here would show a song as a second longer than the
/// clock beside the scrubber says it is -- the same number, disagreeing with itself on one screen.
std::string clock_text(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0) {
        seconds = 0.0;
    }
    const auto total = static_cast<int>(seconds);
    return std::to_string(total / 60) + ":" + (total % 60 < 10 ? "0" : "")
           + std::to_string(total % 60);
}

std::string frames_to_clock(std::int64_t frames)
{
    return clock_text(static_cast<double>(frames) / ts::host::Session::sample_rate);
}

// -- Rows ------------------------------------------------------------------------------------------

/// A read-only row: a title on the left, a selectable value on the right.
///
/// Selectable because everything here is something somebody might want to paste somewhere -- a
/// composer's name into a search, a loop point into a sequencer. The same reasoning, and the same
/// shape, as the ROM setup screen's identity rows.
AdwActionRow* value_row(const char* title, const char* value)
{
    auto* row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);

    GtkWidget* label = gtk_label_new(value);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 40);
    gtk_label_set_xalign(GTK_LABEL(label), 1.0F);
    gtk_widget_add_css_class(label, "dim-label");
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(label, TRUE);

    adw_action_row_add_suffix(row, label);
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
    return row;
}

void add_value(AdwPreferencesGroup* group, const char* title, const std::string& value)
{
    // An em dash rather than a hidden row. A file that says nothing about itself is the common case
    // -- most of them do -- and a window that shrinks to three rows for it looks broken, where one
    // that says "Copyright --" has answered the question.
    adw_preferences_group_add(group,
                              GTK_WIDGET(value_row(title, value.empty() ? "—" : value.c_str())));
}

/// A row that is nothing but its own text, wrapped across the row's full width.
///
/// What a file says about itself is already a label -- "Copyright 1994 Somebody", "Verse 2" -- so
/// there is nothing to put in a title column beside it except the same word over and over.
AdwActionRow* text_row(const char* body)
{
    auto* row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), body);
    adw_preferences_row_set_title_selectable(ADW_PREFERENCES_ROW(row), TRUE);
    adw_action_row_set_title_lines(row, 0);
    // A list box row is activatable by default, which would light this up under the pointer and
    // then do nothing -- the affordance of a button with none of the behaviour. Only the markers
    // earn that, because only they go anywhere.
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
    return row;
}

/// Where a row's seek target is kept, in seconds.
///
/// Hung off the row rather than held in a struct the callback owns, so it dies with the row instead
/// of outliving it -- the page is torn down and rebuilt whenever the file changes, and a heap
/// allocation per marker would have to be tracked through that.
constexpr const char* seek_key = "ts-seek-seconds";

void on_marker_activated(AdwActionRow* row, gpointer user_data)
{
    auto* model = TS_PLAYER_MODEL(user_data);
    const auto* seconds = static_cast<const double*>(g_object_get_data(G_OBJECT(row), seek_key));
    if (seconds != nullptr) {
        ts_player_model_seek(model, *seconds);
    }
}

/// A marker: what it says, where it falls, and a jump to it.
///
/// Activatable, because a marker whose position is known and cannot be reached is a label rather
/// than a place -- "Chorus" is worth showing mostly because it is worth going to. Seeking only, not
/// seek-and-play: the transport keeps whatever state it had, so this scrubs a paused song and jumps
/// a playing one, which is what a scrubber does and what the row is standing in for.
AdwActionRow* marker_row(TsPlayerModel* model, const char* body, std::int64_t frames)
{
    auto* row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), body);
    adw_action_row_set_title_lines(row, 0);

    const double seconds = static_cast<double>(frames) / ts::host::Session::sample_rate;
    GtkWidget* stamp = gtk_label_new(clock_text(seconds).c_str());
    gtk_widget_add_css_class(stamp, "dim-label");
    gtk_widget_add_css_class(stamp, "numeric");
    gtk_widget_set_valign(stamp, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(row, stamp);

    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), TRUE);
    adw_action_row_add_suffix(row, gtk_image_new_from_icon_name("go-next-symbolic"));

    g_object_set_data_full(G_OBJECT(row), seek_key, new double{seconds},
                           [](gpointer held) { delete static_cast<double*>(held); });
    g_signal_connect_object(row, "activated", G_CALLBACK(on_marker_activated), model,
                            G_CONNECT_DEFAULT);
    return row;
}

AdwPreferencesGroup* new_group(AdwPreferencesPage* page, const char* title)
{
    auto* group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(group, title);
    adw_preferences_page_add(page, group);
    return group;
}

// -- The pages ---------------------------------------------------------------------------------------

std::string channel_list(const std::vector<int>& channels)
{
    if (channels.empty()) {
        return {};
    }
    std::string text;
    for (const int channel : channels) {
        if (!text.empty()) {
            text += ", ";
        }
        // One-based, because that is how every sequencer and this program's own mixer number them.
        text += std::to_string(channel + 1);
    }
    return text;
}

std::string container_text(const SongInfo& info)
{
    // `to_smf` reports that it converted, not what it converted from, so a foreign container can
    // only be described as one. Saying so still matters: it explains why a `.xmi` has track names
    // and a format number at all.
    /* TRANSLATORS: %d is the SMF format number, 0, 1 or 2. */
    g_autofree char* text = info.container.empty()
                                ? g_strdup_printf(_("Standard MIDI File, format %d"), info.format)
                                : g_strdup_printf(
                                      _("Converted to Standard MIDI File, format %d"), info.format);
    return text;
}

/// The page is a pure function of the file's own metadata plus the module setting -- it reads no
/// model property, which is what keeps it from picking up the previous song's values when it is
/// rebuilt from `notify::song-name`.
void build_song_page(AdwPreferencesPage* page, const SongInfo& info, TsPlayerModel* model)
{
    // No "Name" row: the window's own subtitle already carries the file name, and repeating it as
    // the first row of the first group says the same thing twice on one screen.
    auto* file_group = new_group(page, _("File"));
    add_value(file_group, _("Container"), container_text(info));
    add_value(file_group, _("Tracks"), std::to_string(info.track_count));
    add_value(file_group, _("Duration"), frames_to_clock(info.length));

    // -- What it asks to be played on --
    //
    // No group description. What "asks for" means is carried by the evidence under it, and a
    // paragraph explaining that Preferences is where the module is chosen belongs in Preferences.
    auto* module_group = new_group(page, _("Module"));

    const std::string wanted = ts::host::vintage_name(info.vintage);
    auto* asks = value_row(_("Asks For"), wanted.empty() ? _("No preference") : wanted.c_str());
    if (!info.vintage_evidence.empty()) {
        // The evidence is the row's own subtitle rather than a second row: it is the reason for
        // the value beside it, not a separate fact.
        g_autofree char* markup = g_markup_escape_text(info.vintage_evidence.c_str(), -1);
        adw_action_row_set_subtitle(asks, markup);
    }
    adw_preferences_group_add(module_group, GTK_WIDGET(asks));

    const int playing_on = g_settings_get_int(ts_settings_get(), "map");
    add_value(module_group, _("Playing On"), ts_tone_map_display_name(playing_on));

    // -- Timing --
    auto* timing_group = new_group(page, _("Timing"));
    std::string tempo;
    if (info.initial_tempo_bpm > 0.0) {
        const int bpm = static_cast<int>(info.initial_tempo_bpm + 0.5);
        // Two whole sentences rather than a suffix appended to the first: the qualifier lands in a
        // different place in different languages, and appending fixes it at the end.
        /* TRANSLATORS: %d is beats per minute. */
        g_autofree char* text = info.tempo_changes > 1
                                    ? g_strdup_printf(_("%d bpm, varying"), bpm)
                                    : g_strdup_printf(_("%d bpm"), bpm);
        tempo = text;
    }
    add_value(timing_group, _("Tempo"), tempo);
    add_value(timing_group, _("Time"), info.time_signature);
    add_value(timing_group, _("Key"), info.key_signature);

    // -- The loop --
    //
    // Only when there is one. A group whose entire content is "there is no loop" is a row saying
    // nothing, and the great majority of files have none.
    if (info.has_loop) {
        auto* loop_group = new_group(page, _("Loop"));
        // Named because the two behave differently at the jump: a soft loop rewinds cleanly, where
        // a hard one's end is inferred and replays controllers the way a seek does.
        /* TRANSLATORS: the two %s are clock positions like "1:23"; this is the span of the loop. */
        g_autofree char* span =
            g_strdup_printf(_("%s – %s"), frames_to_clock(info.loop_start).c_str(),
                            frames_to_clock(info.loop_end).c_str());
        add_value(loop_group, info.loop_soft ? _("Soft") : _("Hard"), span);
    }

    // -- What it says --
    //
    // The text is its own label. Files carry anywhere from one line to a dozen, and a "Text" title
    // repeated down the left of every one of them is a column of the same word.
    const bool has_text =
        !info.copyright.empty() || !info.text.empty() || !info.karaoke_headings.empty();
    if (has_text) {
        auto* text_group = new_group(page, _("Text"));
        if (!info.copyright.empty()) {
            g_autofree char* markup = to_markup(info.copyright, info.encoding);
            adw_preferences_group_add(text_group, GTK_WIDGET(text_row(markup)));
        }
        for (const std::string& line : info.text) {
            g_autofree char* markup = to_markup(line, info.encoding);
            adw_preferences_group_add(text_group, GTK_WIDGET(text_row(markup)));
        }
        for (const std::string& heading : info.karaoke_headings) {
            g_autofree char* markup = to_markup(heading, info.encoding);
            adw_preferences_group_add(text_group, GTK_WIDGET(text_row(markup)));
        }
    }

    if (!info.markers.empty()) {
        auto* marker_group = new_group(page, _("Markers"));
        for (const auto& marker : info.markers) {
            g_autofree char* markup = to_markup(marker.text, info.encoding);
            adw_preferences_group_add(
                marker_group, GTK_WIDGET(marker_row(model, markup, marker.position)));
        }
    }
}

void build_tracks_page(AdwPreferencesPage* page, const SongInfo& info)
{
    // No description. The list is self-evidently the file's tracks, and a note explaining that a
    // track with no notes carries only tempo is answered by the row itself saying "0 notes".
    auto* group = new_group(page, nullptr);

    for (const auto& track : info.tracks) {
        auto* row = ADW_ACTION_ROW(adw_action_row_new());

        g_autofree char* name = to_markup(track.name, info.encoding);
        /* TRANSLATORS: %d is the track number and %s its name, e.g. "3. Bass". */
        g_autofree char* heading =
            g_strdup_printf(_("%d. %s"), track.number, *name != '\0' ? name : _("Untitled"));
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), heading);

        // The instrument name and the channels are two different answers to "what is this track",
        // and files supply one, both or neither.
        g_autofree char* instrument = to_markup(track.instrument, info.encoding);
        const std::string channels = channel_list(track.channels);
        std::string subtitle;
        if (*instrument != '\0') {
            subtitle = instrument;
        }
        if (!channels.empty()) {
            if (!subtitle.empty()) {
                subtitle += " · ";
            }
            // The plural is over how many channels the list names, not over any one number in
            // it, so the joined list is substituted into a form chosen by its length.
            /* TRANSLATORS: %s is a comma-separated list of channel numbers. */
            g_autofree char* text = g_strdup_printf(
                g_dngettext(nullptr, "Channel %s", "Channels %s",
                            static_cast<gulong>(track.channels.size())),
                channels.c_str());
            subtitle += text;
        }
        if (!subtitle.empty()) {
            adw_action_row_set_subtitle(row, subtitle.c_str());
        }

        /* TRANSLATORS: %d is how many notes the track contains. */
        g_autofree char* notes =
            g_strdup_printf(g_dngettext(nullptr, "%d note", "%d notes",
                                        static_cast<gulong>(track.notes)),
                            track.notes);
        GtkWidget* count = gtk_label_new(notes);
        gtk_widget_add_css_class(count, "dim-label");
        gtk_widget_add_css_class(count, "numeric");
        gtk_widget_set_valign(count, GTK_ALIGN_CENTER);
        adw_action_row_add_suffix(row, count);

        adw_preferences_group_add(group, GTK_WIDGET(row));
    }
}

GtkWidget* build_lyrics_page(const SongInfo& info)
{
    g_autofree char* text = to_utf8(info.lyrics, info.encoding);

    if (*text == '\0') {
        auto* empty = ADW_STATUS_PAGE(adw_status_page_new());
        adw_status_page_set_icon_name(empty, "format-justify-left-symbolic");
        adw_status_page_set_title(empty, _("No Lyrics"));
        return GTK_WIDGET(empty);
    }

    // A text view rather than a label: the sheet can run to hundreds of lines, and this is the
    // widget that scrolls one cheaply and lets it be selected and copied out.
    GtkWidget* view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 18);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view), 18);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view), 18);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(view), 18);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), text, -1);

    GtkWidget* scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), view);
    gtk_widget_set_vexpand(scroller, TRUE);
    return scroller;
}

/// An empty page for when nothing is loaded, so the window has something to say rather than
/// showing a set of em dashes.
GtkWidget* build_nothing_loaded()
{
    auto* empty = ADW_STATUS_PAGE(adw_status_page_new());
    adw_status_page_set_icon_name(empty, "audio-x-generic-symbolic");
    adw_status_page_set_title(empty, _("No Song Loaded"));
    return GTK_WIDGET(empty);
}

AdwPreferencesPage* new_scrolling_page()
{
    auto* page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    gtk_widget_set_vexpand(GTK_WIDGET(page), TRUE);
    return page;
}

/// Fills the window from whatever the model currently holds.
///
/// Everything is rebuilt rather than updated in place. The content is a tree whose *shape* depends
/// on the file -- a track per row, a group that exists only when there are markers -- so there is
/// nothing to update in place, and this runs when a file is opened rather than on any tick.
void rebuild(TsSongInfoWindow* self)
{
    while (GtkWidget* page = gtk_widget_get_first_child(GTK_WIDGET(self->pages))) {
        adw_view_stack_remove(self->pages, page);
    }

    g_autofree char* song_name = nullptr;
    g_object_get(self->model, "song-name", &song_name, nullptr);

    if (song_name == nullptr) {
        adw_view_stack_add_titled_with_icon(self->pages, build_nothing_loaded(), "empty",
                                            _("Song"), "audio-x-generic-symbolic");
        adw_window_title_set_title(self->title, _("Song Information"));
        adw_window_title_set_subtitle(self->title, nullptr);
        return;
    }

    const SongInfo& info = ts_player_model_get_song_info(self->model);

    auto* song = new_scrolling_page();
    build_song_page(song, info, self->model);
    adw_view_stack_add_titled_with_icon(self->pages, GTK_WIDGET(song), "song", _("Song"),
                                        "help-about-symbolic");

    auto* tracks = new_scrolling_page();
    build_tracks_page(tracks, info);
    adw_view_stack_add_titled_with_icon(self->pages, GTK_WIDGET(tracks), "tracks", _("Tracks"),
                                        "view-list-symbolic");

    adw_view_stack_add_titled_with_icon(self->pages, build_lyrics_page(info), "lyrics",
                                        _("Lyrics"), "format-justify-left-symbolic");

    adw_window_title_set_title(self->title, _("Song Information"));
    adw_window_title_set_subtitle(self->title, song_name);
}

void on_song_changed(GObject*, GParamSpec*, gpointer user_data)
{
    rebuild(TS_SONG_INFO_WINDOW(user_data));
}

} // namespace

static void ts_song_info_window_class_init(TsSongInfoWindowClass*) {}
static void ts_song_info_window_init(TsSongInfoWindow*) {}

GtkWindow* ts_song_info_window_new(TsPlayerModel* model, GtkWindow* parent)
{
    auto* self = TS_SONG_INFO_WINDOW(g_object_new(TS_TYPE_SONG_INFO_WINDOW, nullptr));
    self->model = model;

    gtk_window_set_title(GTK_WINDOW(self), _("Song Information"));
    // Tall rather than wide: every page here is a single column, and the file group, the module and
    // the timing together already fill more than a 640-high window before any of the file's own
    // text is reached.
    gtk_window_set_default_size(GTK_WINDOW(self), 480, 780);

    // Transient but not modal: the point of a window rather than a dialog is that the player stays
    // usable underneath it, and that this stays open across one file and the next.
    gtk_window_set_transient_for(GTK_WINDOW(self), parent);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(self), TRUE);

    self->title = ADW_WINDOW_TITLE(adw_window_title_new(_("Song Information"), nullptr));
    self->pages = ADW_VIEW_STACK(adw_view_stack_new());

    auto* header = ADW_HEADER_BAR(adw_header_bar_new());
    adw_header_bar_set_title_widget(header, GTK_WIDGET(self->title));

    auto* switcher = ADW_VIEW_SWITCHER(adw_view_switcher_new());
    adw_view_switcher_set_stack(switcher, self->pages);
    adw_view_switcher_set_policy(switcher, ADW_VIEW_SWITCHER_POLICY_WIDE);

    // The switcher goes in a bottom bar rather than the header, because three pages of prose want
    // the window's whole width and the title already carries the file name.
    auto* switcher_bar = ADW_VIEW_SWITCHER_BAR(adw_view_switcher_bar_new());
    adw_view_switcher_bar_set_stack(switcher_bar, self->pages);
    adw_view_switcher_bar_set_reveal(switcher_bar, TRUE);

    auto* toolbar = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
    adw_toolbar_view_add_top_bar(toolbar, GTK_WIDGET(header));
    adw_toolbar_view_add_bottom_bar(toolbar, GTK_WIDGET(switcher_bar));
    adw_toolbar_view_set_content(toolbar, GTK_WIDGET(self->pages));

    adw_window_set_content(ADW_WINDOW(self), GTK_WIDGET(toolbar));

    // Follows the player rather than freezing on the file that was open when it appeared, which is
    // the whole reason this is a window that can be left up.
    g_signal_connect_object(model, "notify::song-name", G_CALLBACK(on_song_changed), self,
                            G_CONNECT_DEFAULT);

    rebuild(self);
    return GTK_WINDOW(self);
}

#include "app/ts-part-object.hpp"

#include "app/ts-tone-map.hpp"

#include "tabulasonora/sequence.hpp"

#include <string>

struct _TsPart {
    GObject parent_instance;

    int index;

    char* label;
    char* name;
    char* tags;

    int voices;
    gboolean muted;
    gboolean soloed;
    gboolean present;
    gboolean drums;
    gboolean dimmed;
};

G_DEFINE_FINAL_TYPE(TsPart, ts_part, G_TYPE_OBJECT)

enum {
    PROP_LABEL = 1,
    PROP_NAME,
    PROP_TAGS,
    PROP_VOICES,
    PROP_MUTED,
    PROP_SOLOED,
    PROP_PRESENT,
    PROP_DRUMS,
    PROP_DIMMED,
    N_PROPS,
};

static GParamSpec* properties[N_PROPS];

static void ts_part_finalize(GObject* object)
{
    auto* self = TS_PART(object);
    g_clear_pointer(&self->label, g_free);
    g_clear_pointer(&self->name, g_free);
    g_clear_pointer(&self->tags, g_free);
    G_OBJECT_CLASS(ts_part_parent_class)->finalize(object);
}

static void ts_part_get_property(GObject* object, guint id, GValue* value, GParamSpec* spec)
{
    auto* self = TS_PART(object);

    switch (id) {
    case PROP_LABEL: g_value_set_string(value, self->label); break;
    case PROP_NAME: g_value_set_string(value, self->name); break;
    case PROP_TAGS: g_value_set_string(value, self->tags); break;
    case PROP_VOICES: g_value_set_int(value, self->voices); break;
    case PROP_MUTED: g_value_set_boolean(value, self->muted); break;
    case PROP_SOLOED: g_value_set_boolean(value, self->soloed); break;
    case PROP_PRESENT: g_value_set_boolean(value, self->present); break;
    case PROP_DRUMS: g_value_set_boolean(value, self->drums); break;
    case PROP_DIMMED: g_value_set_boolean(value, self->dimmed); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID(object, id, spec); break;
    }
}

static void ts_part_class_init(TsPartClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = ts_part_finalize;
    object_class->get_property = ts_part_get_property;

    const GParamFlags read_only =
        static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_LABEL] = g_param_spec_string("label", nullptr, nullptr, nullptr, read_only);
    properties[PROP_NAME] = g_param_spec_string("name", nullptr, nullptr, nullptr, read_only);
    properties[PROP_TAGS] = g_param_spec_string("tags", nullptr, nullptr, nullptr, read_only);
    properties[PROP_VOICES] = g_param_spec_int("voices", nullptr, nullptr, 0, G_MAXINT, 0, read_only);
    properties[PROP_MUTED] = g_param_spec_boolean("muted", nullptr, nullptr, FALSE, read_only);
    properties[PROP_SOLOED] = g_param_spec_boolean("soloed", nullptr, nullptr, FALSE, read_only);
    properties[PROP_PRESENT] = g_param_spec_boolean("present", nullptr, nullptr, FALSE, read_only);
    properties[PROP_DRUMS] = g_param_spec_boolean("drums", nullptr, nullptr, FALSE, read_only);
    properties[PROP_DIMMED] = g_param_spec_boolean("dimmed", nullptr, nullptr, FALSE, read_only);

    g_object_class_install_properties(object_class, N_PROPS, properties);
}

static void ts_part_init(TsPart* self)
{
    self->label = g_strdup("");
    self->name = g_strdup("");
    self->tags = g_strdup("");
}

TsPart* ts_part_new(int index)
{
    auto* self = TS_PART(g_object_new(TS_TYPE_PART, nullptr));
    self->index = index;

    // Port letter and a 1-based channel, the way a mixer labels them: A1..A16, B1..B16, and so on
    // up to D16 when the engine is running four ports.
    const int port = index / ts::Sequence::channel_count;
    const int channel = index % ts::Sequence::channel_count;
    g_free(self->label);
    self->label = g_strdup_printf("%c%d", 'A' + port, channel + 1);

    return self;
}

int ts_part_get_index(TsPart* self) { return self->index; }
gboolean ts_part_get_present(TsPart* self) { return self->present; }
gboolean ts_part_get_muted(TsPart* self) { return self->muted; }
gboolean ts_part_get_soloed(TsPart* self) { return self->soloed; }
int ts_part_get_voices(TsPart* self) { return self->voices; }

namespace {

void set_bool(TsPart* self, gboolean& field, gboolean value, int prop)
{
    if (field != value) {
        field = value;
        g_object_notify_by_pspec(G_OBJECT(self), properties[prop]);
    }
}

void set_int(TsPart* self, int& field, int value, int prop)
{
    if (field != value) {
        field = value;
        g_object_notify_by_pspec(G_OBJECT(self), properties[prop]);
    }
}

void set_string(TsPart* self, char*& field, const char* value, int prop)
{
    if (g_strcmp0(field, value) != 0) {
        g_free(field);
        field = g_strdup(value);
        g_object_notify_by_pspec(G_OBJECT(self), properties[prop]);
    }
}

/// The chips under the instrument name.
///
/// The tone map is per part and per moment rather than a global setting -- a bank LSB names a
/// vintage and an XG System On moves every part at once -- so it is read from the part's own state
/// on every update rather than from the engine's configured map.
std::string tags_for(const ts::host::PartState& state)
{
    std::string tags;

    if (state.drums) {
        tags = state.kit >= 0 ? "Kit " + std::to_string(state.kit) : "Drums";
    }

    const char* map = ts_tone_map_display_name(state.map);
    if (*map != '\0') {
        if (!tags.empty()) {
            tags += " · ";
        }
        tags += map;
    }

    return tags;
}

} // namespace

void ts_part_update(TsPart* self, const ts::host::PartState& state, gboolean dimmed)
{
    set_string(self, self->name, state.name.empty() ? "—" : state.name.c_str(), PROP_NAME);
    set_string(self, self->tags, tags_for(state).c_str(), PROP_TAGS);

    set_int(self, self->voices, state.voices, PROP_VOICES);
    set_bool(self, self->muted, state.muted ? TRUE : FALSE, PROP_MUTED);
    set_bool(self, self->soloed, state.soloed ? TRUE : FALSE, PROP_SOLOED);
    set_bool(self, self->present, state.present ? TRUE : FALSE, PROP_PRESENT);
    set_bool(self, self->drums, state.drums ? TRUE : FALSE, PROP_DRUMS);
    set_bool(self, self->dimmed, dimmed, PROP_DIMMED);
}

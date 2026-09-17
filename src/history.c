#include "history.h"

#include <errno.h>
#include <json-glib/json-glib.h>
#include <glib/gstdio.h>
#include <unistd.h>

static void entry_free(gpointer data)
{
    HistoryEntry *entry = data;
    history_entry_clear(entry);
    g_free(entry);
}

static const gchar *kind_name(CommandKind kind)
{
    switch (kind) {
    case COMMAND_WINDOW:
        return "window";
    case COMMAND_CUSTOM:
        return "custom";
    case COMMAND_SHELL:
    default:
        return "shell";
    }
}

static CommandKind parse_kind(const gchar *value)
{
    if (g_strcmp0(value, "window") == 0) {
        return COMMAND_WINDOW;
    }
    if (g_strcmp0(value, "custom") == 0) {
        return COMMAND_CUSTOM;
    }
    return COMMAND_SHELL;
}

static HistoryStore *history_store_new(guint max_entries)
{
    HistoryStore *history = g_new0(HistoryStore, 1);
    history->max_entries = max_entries;
    history->entries = g_ptr_array_new_with_free_func(entry_free);
    return history;
}

gchar *history_default_path(void)
{
    return g_build_filename(g_get_user_state_dir(), "windowcmd", "history.json", NULL);
}

void history_store_free(HistoryStore *history)
{
    if (!history) {
        return;
    }
    g_clear_pointer(&history->entries, g_ptr_array_unref);
    g_free(history);
}

static const gchar *object_string(JsonObject *object, const gchar *name)
{
    if (!json_object_has_member(object, name)) {
        return NULL;
    }
    return json_object_get_string_member(object, name);
}

gboolean history_load(const gchar *path, guint max_entries, HistoryStore **out, GError **error)
{
    gchar *contents = NULL;
    JsonParser *parser;
    JsonNode *root_node;
    JsonArray *array;
    HistoryStore *history;

    g_return_val_if_fail(out != NULL, FALSE);
    history = history_store_new(max_entries);
    *out = history;
    if (!path || !g_file_get_contents(path, &contents, NULL, error)) {
        if (error && *error && g_error_matches(*error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            g_clear_error(error);
            return TRUE;
        }
        return path == NULL;
    }

    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, contents, -1, error)) {
        g_object_unref(parser);
        g_free(contents);
        return FALSE;
    }
    root_node = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_ARRAY(root_node)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "history root must be an array");
        g_object_unref(parser);
        g_free(contents);
        return FALSE;
    }
    array = json_node_get_array(root_node);
    for (guint i = 0; i < json_array_get_length(array) && history->entries->len < max_entries; i++) {
        JsonObject *object = json_array_get_object_element(array, i);
        const gchar *command;
        const gchar *application_class;
        HistoryEntry *entry;
        if (!object) {
            continue;
        }
        command = object_string(object, "command");
        if (!command || !*command) {
            continue;
        }
        entry = g_new0(HistoryEntry, 1);
        entry->command = g_strdup(command);
        entry->timestamp = json_object_has_member(object, "timestamp") ?
                           json_object_get_int_member(object, "timestamp") : 0;
        entry->uses = json_object_has_member(object, "uses") ?
                      (guint) json_object_get_int_member(object, "uses") : 1;
        application_class = object_string(object, "application_class");
        entry->application_class = g_strdup(application_class ? application_class : "");
        entry->kind = parse_kind(object_string(object, "kind"));
        g_ptr_array_add(history->entries, entry);
    }
    g_object_unref(parser);
    g_free(contents);
    return TRUE;
}

void history_record(HistoryStore *history,
                    const gchar *command,
                    CommandKind kind,
                    const gchar *application_class,
                    gint64 timestamp)
{
    HistoryEntry *entry = NULL;
    if (!history || !command || !*command) {
        return;
    }
    for (guint i = 0; i < history->entries->len; i++) {
        HistoryEntry *candidate = g_ptr_array_index(history->entries, i);
        if (candidate->kind == kind && g_strcmp0(candidate->command, command) == 0 &&
            g_strcmp0(candidate->application_class, application_class) == 0) {
            entry = candidate;
            break;
        }
    }
    if (entry) {
        entry->uses++;
        entry->timestamp = timestamp;
        return;
    }
    entry = g_new0(HistoryEntry, 1);
    entry->command = g_strdup(command);
    entry->timestamp = timestamp;
    entry->uses = 1;
    entry->application_class = g_strdup(application_class ? application_class : "");
    entry->kind = kind;
    g_ptr_array_insert(history->entries, 0, entry);
    while (history->entries->len > history->max_entries) {
        g_ptr_array_remove_index(history->entries, history->entries->len - 1);
    }
}

gboolean history_save(const HistoryStore *history, const gchar *path, GError **error)
{
    JsonBuilder *builder;
    JsonGenerator *generator;
    JsonNode *root;
    gchar *data;
    gchar *directory;
    gchar *template_path;
    gint fd;
    gboolean ok;

    if (!history || !path) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "history path is missing");
        return FALSE;
    }
    directory = g_path_get_dirname(path);
    if (g_mkdir_with_parents(directory, 0700) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "cannot create history directory");
        g_free(directory);
        return FALSE;
    }
    template_path = g_build_filename(directory, ".history.XXXXXX", NULL);
    fd = g_mkstemp(template_path);
    if (fd < 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "cannot create history temporary file");
        g_free(template_path);
        g_free(directory);
        return FALSE;
    }
    close(fd);

    builder = json_builder_new();
    json_builder_begin_array(builder);
    for (guint i = 0; i < history->entries->len; i++) {
        const HistoryEntry *entry = g_ptr_array_index(history->entries, i);
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "command");
        json_builder_add_string_value(builder, entry->command);
        json_builder_set_member_name(builder, "timestamp");
        json_builder_add_int_value(builder, entry->timestamp);
        json_builder_set_member_name(builder, "uses");
        json_builder_add_int_value(builder, entry->uses);
        json_builder_set_member_name(builder, "application_class");
        json_builder_add_string_value(builder, entry->application_class);
        json_builder_set_member_name(builder, "kind");
        json_builder_add_string_value(builder, kind_name(entry->kind));
        json_builder_end_object(builder);
    }
    json_builder_end_array(builder);
    root = json_builder_get_root(builder);
    generator = json_generator_new();
    json_generator_set_root(generator, root);
    data = json_generator_to_data(generator, NULL);
    ok = g_file_set_contents(template_path, data, -1, error) && g_rename(template_path, path) == 0;
    if (!ok && (!error || !*error)) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "cannot replace history file");
    }
    if (!ok) {
        g_remove(template_path);
    }
    json_node_free(root);
    g_object_unref(generator);
    g_object_unref(builder);
    g_free(data);
    g_free(template_path);
    g_free(directory);
    return ok;
}

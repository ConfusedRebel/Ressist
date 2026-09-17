#include "config.h"

#include <errno.h>
#include <json-glib/json-glib.h>
#include <glib/gstdio.h>

static void custom_command_free(gpointer data)
{
    CustomCommand *command = data;
    if (!command) {
        return;
    }
    g_free(command->name);
    g_free(command->command);
    g_free(command->keywords);
    g_free(command);
}

static void alias_free(gpointer data)
{
    CommandAlias *alias = data;
    if (!alias) {
        return;
    }
    g_free(alias->name);
    g_free(alias->command);
    g_free(alias);
}

void config_set_defaults(Config *config)
{
    g_return_if_fail(config != NULL);
    config->width = 640;
    config->max_results = 8;
    config->position = g_strdup("center");
    config->font = g_strdup("Sans 11");
    config->border_radius = 12;
    config->transparency = 0.96;
    config->history_size = 200;
    config->shell = g_strdup(g_getenv("SHELL") ? g_getenv("SHELL") : "/bin/sh");
    config->include_path_commands = TRUE;
    config->path_refresh_seconds = 600;
    config->disabled_builtins = g_ptr_array_new_with_free_func(g_free);
    config->custom_commands = g_ptr_array_new_with_free_func(custom_command_free);
    config->aliases = g_ptr_array_new_with_free_func(alias_free);
    config->ranking.exact = 1.0;
    config->ranking.prefix = 0.8;
    config->ranking.history = 1.0;
    config->ranking.frequency = 0.25;
    config->ranking.recency = 0.25;
    config->ranking.application = 0.5;
    config->ranking.state = 0.5;
}

void config_clear(Config *config)
{
    if (!config) {
        return;
    }
    g_free(config->position);
    g_free(config->font);
    g_free(config->shell);
    g_clear_pointer(&config->disabled_builtins, g_ptr_array_unref);
    g_clear_pointer(&config->custom_commands, g_ptr_array_unref);
    g_clear_pointer(&config->aliases, g_ptr_array_unref);
    config->position = NULL;
    config->font = NULL;
    config->shell = NULL;
}

gchar *config_default_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "windowcmd", "config.json", NULL);
}

gboolean config_write_default(const gchar *path, GError **error)
{
    static const gchar example[] =
        "{\n"
        "  \"width\": 640,\n"
        "  \"max_results\": 8,\n"
        "  \"position\": \"center\",\n"
        "  \"font\": \"Sans 11\",\n"
        "  \"border_radius\": 12,\n"
        "  \"transparency\": 0.96,\n"
        "  \"history_size\": 200,\n"
        "  \"shell\": \"/bin/sh\",\n"
        "  \"include_path_commands\": true,\n"
        "  \"path_refresh_seconds\": 600,\n"
        "  \"disabled_builtins\": [],\n"
        "  \"aliases\": [],\n"
        "  \"custom_commands\": [],\n"
        "  \"ranking\": {\"exact\": 1.0, \"prefix\": 0.8, \"history\": 1.0, "
        "\"frequency\": 0.25, \"recency\": 0.25, \"application\": 0.5, \"state\": 0.5}\n"
        "}\n";
    gchar *directory;
    gboolean ok;

    if (!path || !*path) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "config path is missing");
        return FALSE;
    }
    if (g_file_test(path, G_FILE_TEST_EXISTS)) {
        return TRUE;
    }
    directory = g_path_get_dirname(path);
    if (g_mkdir_with_parents(directory, 0700) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "cannot create config directory");
        g_free(directory);
        return FALSE;
    }
    ok = g_file_set_contents(path, example, -1, error);
    if (ok) {
        g_chmod(path, 0600);
    }
    g_free(directory);
    return ok;
}

static const gchar *string_member(JsonObject *object, const gchar *name)
{
    if (!json_object_has_member(object, name) || !JSON_NODE_HOLDS_VALUE(json_object_get_member(object, name))) {
        return NULL;
    }
    return json_object_get_string_member(object, name);
}

static void load_string_array(JsonObject *root, const gchar *name, GPtrArray *out)
{
    JsonArray *array;
    if (!json_object_has_member(root, name)) {
        return;
    }
    array = json_object_get_array_member(root, name);
    for (guint i = 0; i < json_array_get_length(array); i++) {
        const gchar *value = json_array_get_string_element(array, i);
        if (value && *value) {
            g_ptr_array_add(out, g_strdup(value));
        }
    }
}

static void load_custom_commands(JsonObject *root, Config *config)
{
    JsonArray *array;
    if (!json_object_has_member(root, "custom_commands")) {
        return;
    }
    array = json_object_get_array_member(root, "custom_commands");
    for (guint i = 0; i < json_array_get_length(array); i++) {
        JsonObject *object = json_array_get_object_element(array, i);
        const gchar *name;
        const gchar *command;
        CustomCommand *custom;

        if (!object) {
            continue;
        }
        name = string_member(object, "name");
        command = string_member(object, "command");
        if (!name || !*name || !command || !*command) {
            continue;
        }
        custom = g_new0(CustomCommand, 1);
        custom->name = g_strdup(name);
        custom->command = g_strdup(command);
        custom->keywords = g_strdup(string_member(object, "keywords"));
        g_ptr_array_add(config->custom_commands, custom);
    }
}

static void load_aliases(JsonObject *root, Config *config)
{
    JsonArray *array;
    if (!json_object_has_member(root, "aliases")) {
        return;
    }
    array = json_object_get_array_member(root, "aliases");
    for (guint i = 0; i < json_array_get_length(array); i++) {
        JsonObject *object = json_array_get_object_element(array, i);
        const gchar *name;
        const gchar *command;
        CommandAlias *alias;

        if (!object) {
            continue;
        }
        name = string_member(object, "name");
        command = string_member(object, "command");
        if (!name || !*name || !command || !*command) {
            continue;
        }
        alias = g_new0(CommandAlias, 1);
        alias->name = g_strdup(name);
        alias->command = g_strdup(command);
        g_ptr_array_add(config->aliases, alias);
    }
}

static void load_numbers(JsonObject *root, Config *config)
{
    if (json_object_has_member(root, "width")) {
        gint value = json_object_get_int_member(root, "width");
        if (value >= 240 && value <= 4096) {
            config->width = value;
        }
    }
    if (json_object_has_member(root, "max_results")) {
        gint value = json_object_get_int_member(root, "max_results");
        if (value > 0 && value <= 100) {
            config->max_results = value;
        }
    }
    if (json_object_has_member(root, "border_radius")) {
        gint value = json_object_get_int_member(root, "border_radius");
        if (value >= 0 && value <= 100) {
            config->border_radius = value;
        }
    }
    if (json_object_has_member(root, "transparency")) {
        gdouble value = json_object_get_double_member(root, "transparency");
        if (value >= 0.0 && value <= 1.0) {
            config->transparency = value;
        }
    }
    if (json_object_has_member(root, "history_size")) {
        gint value = json_object_get_int_member(root, "history_size");
        if (value >= 0 && value <= 10000) {
            config->history_size = (guint) value;
        }
    }
    if (json_object_has_member(root, "path_refresh_seconds")) {
        gint value = json_object_get_int_member(root, "path_refresh_seconds");
        if (value >= 1 && value <= 86400) {
            config->path_refresh_seconds = (guint) value;
        }
    }
}

gboolean config_load(const gchar *path, Config *out, GError **error)
{
    gchar *contents = NULL;
    JsonParser *parser;
    JsonNode *root_node;
    JsonObject *root;
    const gchar *value;

    g_return_val_if_fail(out != NULL, FALSE);
    config_set_defaults(out);
    if (!path) {
        return TRUE;
    }
    if (!g_file_get_contents(path, &contents, NULL, error)) {
        if (error && *error && g_error_matches(*error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            g_clear_error(error);
            return TRUE;
        }
        return FALSE;
    }

    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, contents, -1, error)) {
        g_object_unref(parser);
        g_free(contents);
        return FALSE;
    }
    root_node = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root_node)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "config root must be an object");
        g_object_unref(parser);
        g_free(contents);
        return FALSE;
    }
    root = json_node_get_object(root_node);
    load_numbers(root, out);
    value = string_member(root, "position");
    if (value && *value) {
        g_free(out->position);
        out->position = g_strdup(value);
    }
    value = string_member(root, "font");
    if (value && *value) {
        g_free(out->font);
        out->font = g_strdup(value);
    }
    value = string_member(root, "shell");
    if (value && *value) {
        g_free(out->shell);
        out->shell = g_strdup(value);
    }
    if (json_object_has_member(root, "include_path_commands")) {
        out->include_path_commands = json_object_get_boolean_member(root, "include_path_commands");
    }
    load_string_array(root, "disabled_builtins", out->disabled_builtins);
    load_custom_commands(root, out);
    load_aliases(root, out);
    g_object_unref(parser);
    g_free(contents);
    return TRUE;
}

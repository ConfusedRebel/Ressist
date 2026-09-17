#include "providers.h"

#include <glib/gstdio.h>
#include <string.h>

#include "hyprland.h"
#include "parser.h"

static PathCache global_path_cache;

void command_result_free(CommandResult *result)
{
    if (!result) {
        return;
    }
    command_result_clear(result);
    g_free(result);
}

static void result_free(gpointer data)
{
    command_result_free(data);
}

static CommandResult *result_new(const gchar *id,
                                 const gchar *title,
                                 const gchar *description,
                                 const gchar *keywords,
                                 const gchar *command_text,
                                 CommandKind kind)
{
    CommandResult *result = g_new0(CommandResult, 1);
    result->id = g_strdup(id);
    result->title = g_strdup(title);
    result->description = g_strdup(description);
    result->keywords = g_strdup(keywords);
    result->command_text = g_strdup(command_text);
    result->provider = g_strdup(kind == COMMAND_WINDOW ? "Hyprland" :
                                 kind == COMMAND_CUSTOM ? "Custom" : "PATH");
    result->kind = kind;
    return result;
}

CommandResult *provider_shell_result(const gchar *command)
{
    gchar *title;
    CommandResult *result;
    if (!command || !*command) {
        return NULL;
    }
    title = g_strdup_printf("RUN %s", command);
    result = result_new("shell-input", title, "Run this shell command with target environment",
                        command, command, COMMAND_SHELL);
    g_free(title);
    return result;
}

static gboolean disabled(const Config *config, const gchar *id)
{
    for (guint i = 0; config && i < config->disabled_builtins->len; i++) {
        if (g_strcmp0(g_ptr_array_index(config->disabled_builtins, i), id) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

static void add_builtin(GPtrArray *results,
                        const Config *config,
                        const WindowSnapshot *target,
                        const gchar *id,
                        const gchar *title,
                        const gchar *description,
                        const gchar *keywords,
                        const gchar *command)
{
    CommandResult *result;
    if (disabled(config, id)) {
        return;
    }
    result = result_new(id, title, description, keywords, command, COMMAND_WINDOW);
    if (target && target->class_name && *target->class_name &&
        g_strstr_len(description, -1, target->class_name)) {
        result->base_score += 8;
    }
    g_ptr_array_add(results, result);
}

static void add_window_builtins(GPtrArray *results,
                                const Config *config,
                                const WindowSnapshot *target)
{
    gchar *description;
    const gchar *fullscreen_title = target->fullscreen ? "EXIT FULLSCREEN" : "FULLSCREEN";
    const gchar *floating_title = target->floating ? "TILE" : "FLOAT";
    const gchar *pin_title = target->pinned ? "UNPIN" : "PIN";

    description = g_strdup_printf("Close %s", target->title && *target->title ? target->title : "the target window");
    add_builtin(results, config, target, "close", "CLOSE", description, "close window", "close");
    g_free(description);
    description = g_strdup_printf("Kill %s", target->class_name && *target->class_name ? target->class_name : "the target window");
    add_builtin(results, config, target, "kill", "KILL", description, "terminate force", "kill");
    g_free(description);
    description = g_strdup_printf("Toggle floating mode for %s", target->class_name && *target->class_name ? target->class_name : "the target window");
    add_builtin(results, config, target, "float", floating_title, description, "float tile floating tiled", "float");
    g_free(description);
    description = g_strdup_printf("Change fullscreen mode for %s", target->class_name && *target->class_name ? target->class_name : "the target window");
    add_builtin(results, config, target, "fullscreen", fullscreen_title, description, "fullscreen exit fullscreen", "fullscreen");
    g_free(description);
    description = g_strdup_printf("%s %s", pin_title, target->class_name && *target->class_name ? target->class_name : "window");
    add_builtin(results, config, target, "pin", pin_title, description, "pin unpin always on top", target->pinned ? "unpin" : "pin");
    g_free(description);
    add_builtin(results, config, target, "center", "CENTER", "Center the target window", "center position", "center");
    add_builtin(results, config, target, "focus", "FOCUS", "Focus the target window", "focus activate", "focus");
    add_builtin(results, config, target, "pseudo", "PSEUDO", "Toggle pseudo tiling", "pseudo tiling", "pseudo");
    add_builtin(results, config, target, "split", "SPLIT", "Toggle split locking", "split layout", "split");
    add_builtin(results, config, target, "move-left", "MOVE LEFT", "Move the window left", "move direction", "move left");
    add_builtin(results, config, target, "move-right", "MOVE RIGHT", "Move the window right", "move direction", "move right");
    add_builtin(results, config, target, "move-up", "MOVE UP", "Move the window up", "move direction", "move up");
    add_builtin(results, config, target, "move-down", "MOVE DOWN", "Move the window down", "move direction", "move down");
    add_builtin(results, config, target, "opacity", "OPACITY", "Set window opacity, for example: opacity 0.9", "transparent alpha", "opacity 0.9");
    add_builtin(results, config, target, "monitor", "MONITOR", "Move the window to another monitor, for example: monitor HDMI-A-1", "output display", "monitor 0");

    for (gint workspace = 1; workspace <= 10; workspace++) {
        gchar *id = g_strdup_printf("workspace-%d", workspace);
        gchar *title = g_strdup_printf("WORKSPACE %d", workspace);
        gchar *command = g_strdup_printf("workspace %d", workspace);
        gchar *description = g_strdup_printf("Move %s to workspace %d", target->class_name, workspace);
        add_builtin(results, config, target, id, title, description, "workspace move", command);
        g_free(id);
        g_free(title);
        g_free(command);
        g_free(description);
    }
}

void path_cache_clear(PathCache *cache)
{
    if (!cache) {
        return;
    }
    g_free(cache->path);
    g_clear_pointer(&cache->commands, g_hash_table_unref);
    cache->path = NULL;
    cache->refreshed_at = 0;
}

gboolean path_cache_refresh_if_needed(PathCache *cache,
                                      const gchar *path,
                                      guint refresh_seconds,
                                      GError **error)
{
    gint64 now = g_get_monotonic_time();
    gchar **directories;

    if (!cache || !path) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "PATH is missing");
        return FALSE;
    }
    if (cache->commands && g_strcmp0(cache->path, path) == 0 &&
        now - cache->refreshed_at < (gint64) refresh_seconds * G_USEC_PER_SEC) {
        return TRUE;
    }
    path_cache_clear(cache);
    cache->path = g_strdup(path);
    cache->refreshed_at = now;
    cache->commands = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    directories = g_strsplit(path, G_SEARCHPATH_SEPARATOR_S, -1);
    for (guint i = 0; directories[i]; i++) {
        GDir *directory;
        const gchar *name;
        if (!*directories[i] || !(directory = g_dir_open(directories[i], 0, NULL))) {
            continue;
        }
        while ((name = g_dir_read_name(directory))) {
            gchar *full_path;
            if (g_hash_table_contains(cache->commands, name)) {
                continue;
            }
            full_path = g_build_filename(directories[i], name, NULL);
            if (g_file_test(full_path, G_FILE_TEST_IS_REGULAR | G_FILE_TEST_IS_EXECUTABLE)) {
                g_hash_table_insert(cache->commands, g_strdup(name), full_path);
            } else {
                g_free(full_path);
            }
        }
        g_dir_close(directory);
    }
    g_strfreev(directories);
    return TRUE;
}

static void add_path_commands(GPtrArray *results, const Config *config)
{
    const gchar *path = g_getenv("PATH");
    GHashTableIter iter;
    gpointer key;
    gpointer value;

    if (!config->include_path_commands || !path ||
        !path_cache_refresh_if_needed(&global_path_cache, path, config->path_refresh_seconds, NULL)) {
        return;
    }
    g_hash_table_iter_init(&iter, global_path_cache.commands);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        CommandResult *result = result_new(key, key, "Run executable from PATH", key, key, COMMAND_SHELL);
        result->icon = g_strdup(value);
        g_ptr_array_add(results, result);
    }
}

static void add_custom_commands(GPtrArray *results, const Config *config)
{
    for (guint i = 0; i < config->custom_commands->len; i++) {
        CustomCommand *custom = g_ptr_array_index(config->custom_commands, i);
        CommandResult *result = result_new(custom->name, custom->name,
                                           "Run configured command", custom->keywords,
                                           custom->command, COMMAND_CUSTOM);
        g_ptr_array_add(results, result);
    }
    for (guint i = 0; i < config->aliases->len; i++) {
        CommandAlias *alias = g_ptr_array_index(config->aliases, i);
        CommandResult *result = result_new(alias->name, alias->name,
                                           "Run configured alias", "alias command",
                                           alias->command, COMMAND_CUSTOM);
        g_ptr_array_add(results, result);
    }
}

gboolean provider_collect(const Config *config,
                          const WindowSnapshot *target,
                          const HistoryStore *history,
                          GPtrArray **out,
                          GError **error)
{
    (void) history;
    if (!config || !target || !out) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "provider inputs are missing");
        return FALSE;
    }
    *out = g_ptr_array_new_with_free_func(result_free);
    add_window_builtins(*out, config, target);
    add_path_commands(*out, config);
    add_custom_commands(*out, config);
    return TRUE;
}

static const gchar *target_value(const gchar *name, const WindowSnapshot *target)
{
    if (g_strcmp0(name, "HYPR_TARGET_ADDRESS") == 0) return g_strdup(target->address);
    if (g_strcmp0(name, "HYPR_TARGET_CLASS") == 0) return g_strdup(target->class_name);
    if (g_strcmp0(name, "HYPR_TARGET_TITLE") == 0) return g_strdup(target->title);
    if (g_strcmp0(name, "HYPR_TARGET_PID") == 0) return g_strdup_printf("%d", target->pid);
    if (g_strcmp0(name, "HYPR_TARGET_WORKSPACE") == 0) return g_strdup(target->workspace_name);
    return NULL;
}

gboolean expand_target_template(const gchar *template_text,
                                const WindowSnapshot *target,
                                gchar **out,
                                GError **error)
{
    GString *expanded;
    const gchar *cursor;

    if (!template_text || !target || !out) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "template inputs are missing");
        return FALSE;
    }
    expanded = g_string_new(NULL);
    cursor = template_text;
    while (*cursor) {
        const gchar *start = g_strstr_len(cursor, -1, "${HYPR_TARGET_");
        const gchar *end;
        gchar *name;
        const gchar *value;
        if (!start) {
            g_string_append(expanded, cursor);
            break;
        }
        g_string_append_len(expanded, cursor, start - cursor);
        end = strchr(start, '}');
        if (!end) {
            g_string_free(expanded, TRUE);
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "unterminated target template");
            return FALSE;
        }
        name = g_strndup(start + 2, end - start - 2);
        value = target_value(name, target);
        g_free(name);
        if (!value) {
            g_string_free(expanded, TRUE);
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "unknown target template variable");
            return FALSE;
        }
        g_string_append(expanded, value);
        g_free((gpointer) value);
        cursor = end + 1;
    }
    *out = g_string_free(expanded, FALSE);
    return TRUE;
}

gboolean command_result_execute(const CommandResult *result,
                                const WindowSnapshot *target,
                                const Config *config,
                                GError **error)
{
    if (!result || !target || !config) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "execution inputs are missing");
        return FALSE;
    }
    if (result->kind == COMMAND_WINDOW) {
        ParsedAction action = {0};
        gboolean ok = parse_window_command(result->command_text, target, &action, error);
        if (ok) {
            ok = hyprland_dispatch_window(target, &action, error);
        }
        parsed_action_clear(&action);
        return ok;
    }

    gchar *command = NULL;
    gchar **env = g_get_environ();
    gchar *pid = g_strdup_printf("%d", target->pid);
    gchar *workspace = g_strdup_printf("%d", target->workspace);
    const gchar *shell = config->shell && *config->shell ? config->shell : "/bin/sh";
    gchar *argv[] = {(gchar *) shell, "-c", NULL, NULL};
    gboolean ok;

    if (result->kind == COMMAND_CUSTOM) {
        ok = expand_target_template(result->command_text, target, &command, error);
        if (!ok) {
            g_strfreev(env);
            g_free(pid);
            g_free(workspace);
            return FALSE;
        }
    } else {
        command = g_strdup(result->command_text);
    }
    env = g_environ_setenv(env, "HYPR_TARGET_ADDRESS", target->address, TRUE);
    env = g_environ_setenv(env, "HYPR_TARGET_CLASS", target->class_name, TRUE);
    env = g_environ_setenv(env, "HYPR_TARGET_TITLE", target->title, TRUE);
    env = g_environ_setenv(env, "HYPR_TARGET_PID", pid, TRUE);
    env = g_environ_setenv(env, "HYPR_TARGET_WORKSPACE", workspace, TRUE);
    argv[2] = command;
    ok = g_spawn_async(NULL, argv, env, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, error);
    g_strfreev(env);
    g_free(command);
    g_free(pid);
    g_free(workspace);
    return ok;
}

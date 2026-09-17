#include "hyprland.h"

#include <json-glib/json-glib.h>

static gboolean parse_json(const gchar *json, JsonNode **root, GError **error)
{
    JsonParser *parser = json_parser_new();
    gboolean ok = json_parser_load_from_data(parser, json, -1, error);
    if (ok) {
        *root = json_node_copy(json_parser_get_root(parser));
    }
    g_object_unref(parser);
    return ok;
}

static gchar *json_string(JsonObject *object, const gchar *name)
{
    return json_object_has_member(object, name) ?
           g_strdup(json_object_get_string_member(object, name)) : g_strdup("");
}

static gint json_int(JsonObject *object, const gchar *name)
{
    return json_object_has_member(object, name) ? json_object_get_int_member(object, name) : 0;
}

static gboolean json_bool(JsonObject *object, const gchar *name)
{
    return json_object_has_member(object, name) && json_object_get_boolean_member(object, name);
}

static void parse_workspace(JsonObject *object, WindowSnapshot *out)
{
    if (json_object_has_member(object, "workspace")) {
        JsonObject *workspace = json_object_get_object_member(object, "workspace");
        out->workspace = json_int(workspace, "id");
        out->workspace_name = json_string(workspace, "name");
    } else {
        out->workspace_name = g_strdup("");
    }
}

gboolean hyprland_parse_active_json(const gchar *json,
                                    WindowSnapshot *out,
                                    GError **error)
{
    JsonNode *root = NULL;
    JsonObject *object;

    if (!json || !out || !parse_json(json, &root, error) || !JSON_NODE_HOLDS_OBJECT(root)) {
        if (root) {
            json_node_free(root);
        }
        if (!error || !*error) {
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "active window JSON must be an object");
        }
        return FALSE;
    }
    object = json_node_get_object(root);
    window_snapshot_clear(out);
    out->address = json_string(object, "address");
    out->class_name = json_string(object, "class");
    out->title = json_string(object, "title");
    out->pid = json_int(object, "pid");
    out->monitor = json_int(object, "monitor");
    out->floating = json_bool(object, "floating");
    out->fullscreen = json_int(object, "fullscreen") != 0;
    out->pinned = json_bool(object, "pinned");
    out->pseudo = json_bool(object, "pseudo");
    out->split_locked = json_bool(object, "splitLocked");
    parse_workspace(object, out);
    json_node_free(root);
    if (!*out->address) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "active window has no address");
        window_snapshot_clear(out);
        return FALSE;
    }
    return TRUE;
}

gboolean hyprland_address_in_clients_json(const gchar *json,
                                          const gchar *address,
                                          gboolean *exists,
                                          GError **error)
{
    JsonNode *root = NULL;
    JsonArray *clients;

    if (!exists || !address || !parse_json(json, &root, error) || !JSON_NODE_HOLDS_ARRAY(root)) {
        if (root) {
            json_node_free(root);
        }
        if (!error || !*error) {
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "clients JSON must be an array");
        }
        return FALSE;
    }
    *exists = FALSE;
    clients = json_node_get_array(root);
    for (guint i = 0; i < json_array_get_length(clients); i++) {
        JsonObject *client = json_array_get_object_element(clients, i);
        if (client && g_strcmp0(json_object_get_string_member(client, "address"), address) == 0) {
            *exists = TRUE;
            break;
        }
    }
    json_node_free(root);
    return TRUE;
}

static gchar *address_selector(const WindowSnapshot *target)
{
    return g_strdup_printf("address:%s", target->address);
}

gboolean hyprland_build_dispatch_argv(const WindowSnapshot *target,
                                      const ParsedAction *action,
                                      gchar ***argv,
                                      GError **error)
{
    gchar *selector;
    gchar *argument = NULL;
    const gchar *dispatcher = NULL;
    GPtrArray *parts;

    if (!target || !target->address || !*target->address || !action || !argv) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "target and action are required");
        return FALSE;
    }
    selector = address_selector(target);
    switch (action->kind) {
    case ACTION_CLOSE: dispatcher = "closewindow"; break;
    case ACTION_KILL: dispatcher = "killwindow"; break;
    case ACTION_TOGGLE_FLOATING: dispatcher = "togglefloating"; break;
    case ACTION_TOGGLE_FULLSCREEN: dispatcher = "fullscreen"; break;
    case ACTION_PIN: dispatcher = "pin"; break;
    case ACTION_UNPIN: dispatcher = "pin"; break;
    case ACTION_CENTER: dispatcher = "centerwindow"; break;
    case ACTION_FOCUS: dispatcher = "focuswindow"; break;
    case ACTION_TOGGLE_PSEUDO: dispatcher = "pseudo"; break;
    case ACTION_TOGGLE_SPLIT: dispatcher = "togglesplit"; break;
    case ACTION_MOVE_DIRECTION: dispatcher = "movewindow"; argument = g_strdup(action->direction); break;
    case ACTION_WORKSPACE: dispatcher = "movetoworkspace"; argument = g_strdup_printf("%s,%s", action->workspace, selector); break;
    case ACTION_MOVE_WORKSPACE_SILENT: dispatcher = "movetoworkspacesilent"; argument = g_strdup_printf("%s,%s", action->workspace, selector); break;
    case ACTION_RESIZE: dispatcher = "resizeactive"; argument = g_strdup_printf("exact %d %d,%s", action->width, action->height, selector); break;
    case ACTION_OPACITY: dispatcher = "setprop"; argument = g_strdup_printf("opacity %g", action->opacity); break;
    case ACTION_MONITOR: dispatcher = "movewindow"; argument = g_strdup_printf("mon:%s,%s", action->monitor, selector); break;
    case ACTION_TOGGLE_TOP: dispatcher = "pin"; break;
    default: break;
    }
    if (!dispatcher) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "unsupported window action");
        g_free(selector);
        return FALSE;
    }

    parts = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(parts, g_strdup("hyprctl"));
    if (g_strcmp0(dispatcher, "setprop") == 0) {
        g_ptr_array_add(parts, g_strdup("setprop"));
        g_ptr_array_add(parts, g_strdup(selector));
        g_ptr_array_add(parts, g_strdup("opacity"));
        g_ptr_array_add(parts, g_strdup_printf("%g", action->opacity));
    } else {
        g_ptr_array_add(parts, g_strdup("dispatch"));
        g_ptr_array_add(parts, g_strdup(dispatcher));
        g_ptr_array_add(parts, argument ? g_strdup(argument) : g_strdup(selector));
    }
    g_ptr_array_add(parts, NULL);
    *argv = (gchar **) g_ptr_array_free(parts, FALSE);
    g_free(argument);
    g_free(selector);
    return TRUE;
}

static gboolean run_hyprctl(gchar *const *argv, gchar **stdout_text, GError **error)
{
    gint status = 0;
    gchar *stderr_text = NULL;
    gboolean ok = g_spawn_sync(NULL, (gchar **) argv, NULL, G_SPAWN_SEARCH_PATH,
                               NULL, NULL, stdout_text, &stderr_text, &status, error);
    if (ok && status != 0) {
        g_set_error(error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED,
                    "hyprctl failed: %s", stderr_text ? stderr_text : "unknown error");
        ok = FALSE;
    }
    g_free(stderr_text);
    return ok;
}

gboolean hyprland_capture_active(WindowSnapshot *out, GError **error)
{
    gchar *const argv[] = {"hyprctl", "activewindow", "-j", NULL};
    gchar *output = NULL;
    gboolean ok = run_hyprctl(argv, &output, error);
    if (ok) {
        ok = hyprland_parse_active_json(output, out, error);
    }
    g_free(output);
    return ok;
}

gboolean hyprland_target_exists(const gchar *address, gboolean *exists, GError **error)
{
    gchar *const argv[] = {"hyprctl", "clients", "-j", NULL};
    gchar *output = NULL;
    gboolean ok = run_hyprctl(argv, &output, error);
    if (ok) {
        ok = hyprland_address_in_clients_json(output, address, exists, error);
    }
    g_free(output);
    return ok;
}

gboolean hyprland_dispatch_window(const WindowSnapshot *target,
                                  const ParsedAction *action,
                                  GError **error)
{
    gchar **argv = NULL;
    gboolean ok;
    if (!hyprland_build_dispatch_argv(target, action, &argv, error)) {
        return FALSE;
    }
    ok = run_hyprctl(argv, NULL, error);
    g_strfreev(argv);
    return ok;
}

gboolean hyprland_notify(const gchar *message, GError **error)
{
    gchar *const argv[] = {"hyprctl", "notify", "-1", "3000", "rgb(88c0d0)", (gchar *) message, NULL};
    return run_hyprctl(argv, NULL, error);
}

void hyprland_refresh_capabilities(void)
{
}

#include "parser.h"

#include <errno.h>
#include <stdlib.h>

G_DEFINE_QUARK(windowcmd-error, windowcmd_error)

static gboolean fail_invalid(GError **error, const gchar *message)
{
    g_set_error(error, WINDOWCMD_ERROR, WINDOWCMD_ERROR_INVALID_ARGUMENT, "%s", message);
    return FALSE;
}

static gboolean parse_positive_int(const gchar *value, gint *out)
{
    gchar *end = NULL;
    gint64 parsed;

    errno = 0;
    parsed = g_ascii_strtoll(value, &end, 10);
    if (errno || end == value || *end != '\0' || parsed <= 0 || parsed > G_MAXINT) {
        return FALSE;
    }
    *out = (gint) parsed;
    return TRUE;
}

static gboolean parse_opacity(const gchar *value, gdouble *out)
{
    gchar *end = NULL;
    gdouble parsed;

    errno = 0;
    parsed = g_ascii_strtod(value, &end);
    if (errno || end == value || *end != '\0' || parsed < 0.0 || parsed > 1.0) {
        return FALSE;
    }
    *out = parsed;
    return TRUE;
}

static gchar **compact_tokens(const gchar *text, guint *count)
{
    gchar **raw = g_strsplit_set(text, " \t\r\n", -1);
    GPtrArray *tokens = g_ptr_array_new_with_free_func(g_free);

    for (guint i = 0; raw[i]; i++) {
        if (*raw[i]) {
            g_ptr_array_add(tokens, g_strdup(raw[i]));
        }
    }
    g_strfreev(raw);
    g_ptr_array_add(tokens, NULL);
    *count = tokens->len - 1;
    return (gchar **) g_ptr_array_free(tokens, FALSE);
}

gboolean parse_window_command(const gchar *text,
                             const WindowSnapshot *target,
                             ParsedAction *out,
                             GError **error)
{
    guint count = 0;
    gchar **tokens;
    (void) target;

    if (!text || !out) {
        return fail_invalid(error, "command is empty");
    }
    parsed_action_clear(out);
    tokens = compact_tokens(text, &count);
    if (count == 0) {
        g_strfreev(tokens);
        return fail_invalid(error, "command is empty");
    }

    if (g_strcmp0(tokens[0], "workspace") == 0 && count == 2) {
        out->kind = ACTION_WORKSPACE;
        out->workspace = g_strdup(tokens[1]);
    } else if (g_strcmp0(tokens[0], "move") == 0 && count == 3 &&
               g_strcmp0(tokens[1], "workspace") == 0) {
        out->kind = ACTION_MOVE_WORKSPACE_SILENT;
        out->workspace = g_strdup(tokens[2]);
    } else if (g_strcmp0(tokens[0], "resize") == 0 && count == 3) {
        out->kind = ACTION_RESIZE;
        if (!parse_positive_int(tokens[1], &out->width) ||
            !parse_positive_int(tokens[2], &out->height)) {
            g_strfreev(tokens);
            parsed_action_clear(out);
            return fail_invalid(error, "resize expects two positive integers");
        }
    } else if (g_strcmp0(tokens[0], "opacity") == 0 && count == 2) {
        out->kind = ACTION_OPACITY;
        if (!parse_opacity(tokens[1], &out->opacity)) {
            g_strfreev(tokens);
            parsed_action_clear(out);
            return fail_invalid(error, "opacity expects a number from 0 to 1");
        }
    } else if ((g_strcmp0(tokens[0], "float") == 0 ||
                g_strcmp0(tokens[0], "tile") == 0 ||
                (g_strcmp0(tokens[0], "toggle") == 0 && count == 2 &&
                 g_strcmp0(tokens[1], "floating") == 0)) && count == 1) {
        out->kind = ACTION_TOGGLE_FLOATING;
    } else if (g_strcmp0(tokens[0], "toggle") == 0 && count == 2 &&
               g_strcmp0(tokens[1], "floating") == 0) {
        out->kind = ACTION_TOGGLE_FLOATING;
    } else if (g_strcmp0(tokens[0], "fullscreen") == 0 && count == 1) {
        out->kind = ACTION_TOGGLE_FULLSCREEN;
    } else if (g_strcmp0(tokens[0], "exit") == 0 && count == 2 &&
               g_strcmp0(tokens[1], "fullscreen") == 0) {
        out->kind = ACTION_TOGGLE_FULLSCREEN;
    } else if (g_strcmp0(tokens[0], "close") == 0 && count == 1) {
        out->kind = ACTION_CLOSE;
    } else if (g_strcmp0(tokens[0], "kill") == 0 && count == 1) {
        out->kind = ACTION_KILL;
    } else if (g_strcmp0(tokens[0], "focus") == 0 && count == 1) {
        out->kind = ACTION_FOCUS;
    } else {
        g_strfreev(tokens);
        return fail_invalid(error, "unknown command or invalid arguments");
    }

    g_strfreev(tokens);
    return TRUE;
}

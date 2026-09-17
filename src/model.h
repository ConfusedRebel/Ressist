#ifndef WINDOWCMD_MODEL_H
#define WINDOWCMD_MODEL_H

#include <glib.h>

typedef struct {
    gchar *address;
    gchar *class_name;
    gchar *title;
    gint pid;
    gint workspace;
    gchar *workspace_name;
    gint monitor;
    gchar *monitor_name;
    gboolean floating;
    gboolean fullscreen;
    gboolean pinned;
    gboolean pseudo;
    gboolean split_locked;
} WindowSnapshot;

typedef enum {
    COMMAND_WINDOW,
    COMMAND_SHELL,
    COMMAND_CUSTOM,
} CommandKind;

typedef enum {
    ACTION_CLOSE,
    ACTION_KILL,
    ACTION_TOGGLE_FLOATING,
    ACTION_TOGGLE_FULLSCREEN,
    ACTION_PIN,
    ACTION_UNPIN,
    ACTION_CENTER,
    ACTION_WORKSPACE,
    ACTION_MOVE_WORKSPACE_SILENT,
    ACTION_MOVE_DIRECTION,
    ACTION_RESIZE,
    ACTION_FOCUS,
    ACTION_TOGGLE_PSEUDO,
    ACTION_TOGGLE_SPLIT,
    ACTION_OPACITY,
    ACTION_MONITOR,
    ACTION_TOGGLE_TOP,
} WindowActionKind;

typedef struct {
    WindowActionKind kind;
    gchar *workspace;
    gint width;
    gint height;
    gdouble opacity;
    gchar *monitor;
    gchar *direction;
} ParsedAction;

typedef struct {
    gchar *command;
    gint64 timestamp;
    guint uses;
    gchar *application_class;
    CommandKind kind;
} HistoryEntry;

typedef struct {
    gchar *id;
    gchar *title;
    gchar *description;
    gchar *keywords;
    gchar *icon;
    gchar *provider;
    gchar *command_text;
    gint base_score;
    CommandKind kind;
} CommandResult;

static inline void window_snapshot_clear(WindowSnapshot *snapshot)
{
    if (!snapshot) {
        return;
    }
    g_clear_pointer(&snapshot->address, g_free);
    g_clear_pointer(&snapshot->class_name, g_free);
    g_clear_pointer(&snapshot->title, g_free);
    g_clear_pointer(&snapshot->workspace_name, g_free);
    g_clear_pointer(&snapshot->monitor_name, g_free);
}

static inline void parsed_action_clear(ParsedAction *action)
{
    if (!action) {
        return;
    }
    g_clear_pointer(&action->workspace, g_free);
    g_clear_pointer(&action->monitor, g_free);
    g_clear_pointer(&action->direction, g_free);
    action->kind = ACTION_CLOSE;
    action->width = 0;
    action->height = 0;
    action->opacity = 0;
}

static inline void history_entry_clear(HistoryEntry *entry)
{
    if (!entry) {
        return;
    }
    g_clear_pointer(&entry->command, g_free);
    g_clear_pointer(&entry->application_class, g_free);
}

static inline void command_result_clear(CommandResult *result)
{
    if (!result) {
        return;
    }
    g_clear_pointer(&result->id, g_free);
    g_clear_pointer(&result->title, g_free);
    g_clear_pointer(&result->description, g_free);
    g_clear_pointer(&result->keywords, g_free);
    g_clear_pointer(&result->icon, g_free);
    g_clear_pointer(&result->provider, g_free);
    g_clear_pointer(&result->command_text, g_free);
}

#endif

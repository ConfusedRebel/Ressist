#ifndef WINDOWCMD_HISTORY_H
#define WINDOWCMD_HISTORY_H

#include "model.h"

typedef struct {
    GPtrArray *entries;
    guint max_entries;
} HistoryStore;

gchar *history_default_path(void);
gboolean history_load(const gchar *path, guint max_entries, HistoryStore **out, GError **error);
void history_store_free(HistoryStore *history);
void history_record(HistoryStore *history,
                    const gchar *command,
                    CommandKind kind,
                    const gchar *application_class,
                    gint64 timestamp);
gboolean history_save(const HistoryStore *history, const gchar *path, GError **error);

#endif

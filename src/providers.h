#ifndef WINDOWCMD_PROVIDERS_H
#define WINDOWCMD_PROVIDERS_H

#include "config.h"
#include "history.h"

typedef struct {
    gchar *path;
    gint64 refreshed_at;
    GHashTable *commands;
} PathCache;

CommandResult *provider_shell_result(const gchar *command);
void command_result_free(CommandResult *result);
void path_cache_clear(PathCache *cache);
gboolean path_cache_refresh_if_needed(PathCache *cache,
                                      const gchar *path,
                                      guint refresh_seconds,
                                      GError **error);
gboolean provider_collect(const Config *config,
                          const WindowSnapshot *target,
                          const HistoryStore *history,
                          GPtrArray **out,
                          GError **error);
gboolean expand_target_template(const gchar *template_text,
                                const WindowSnapshot *target,
                                gchar **out,
                                GError **error);
gboolean command_result_execute(const CommandResult *result,
                                const WindowSnapshot *target,
                                const Config *config,
                                GError **error);

#endif

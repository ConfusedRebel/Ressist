#ifndef WINDOWCMD_RANKING_H
#define WINDOWCMD_RANKING_H

#include "model.h"

gint fuzzy_score(const gchar *query, const gchar *haystack);
gdouble rank_result(const CommandResult *result,
                    const gchar *query,
                    const WindowSnapshot *target,
                    const HistoryEntry *history);

#endif

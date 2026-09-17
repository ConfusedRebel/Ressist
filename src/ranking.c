#include "ranking.h"

static gchar lower_ascii(gchar value)
{
    return (value >= 'A' && value <= 'Z') ? (gchar) (value + ('a' - 'A')) : value;
}

gint fuzzy_score(const gchar *query, const gchar *haystack)
{
    guint matched = 0;
    guint position = 0;
    guint gaps = 0;
    gboolean prefix = TRUE;
    gboolean contiguous = TRUE;
    gboolean exact;
    guint previous_position = G_MAXUINT;

    if (!query || !haystack) {
        return -1;
    }
    if (!*query) {
        return 0;
    }

    for (const gchar *needle = query; *needle; needle++) {
        gboolean found = FALSE;
        for (; haystack[position]; position++) {
            if (lower_ascii(*needle) == lower_ascii(haystack[position])) {
                if (matched > 0 && position != previous_position + 1) {
                    contiguous = FALSE;
                }
                if (position != matched) {
                    prefix = FALSE;
                }
                previous_position = position;
                position++;
                matched++;
                found = TRUE;
                break;
            }
        }
        if (!found) {
            return -1;
        }
    }

    exact = haystack[position] == '\0';
    gaps = position - matched;
    if (exact) {
        return 1200 + (gint) matched * 10;
    }
    if (prefix && contiguous) {
        return 1000 + (gint) matched * 10;
    }
    if (prefix) {
        return 700 + (gint) matched * 10 - (gint) gaps;
    }
    if (contiguous) {
        return 500 + (gint) matched * 10 - (gint) gaps;
    }
    return 100 + (gint) matched * 10 - (gint) gaps;
}

gdouble rank_result(const CommandResult *result,
                    const gchar *query,
                    const WindowSnapshot *target,
                    const HistoryEntry *history)
{
    gchar *haystack;
    gint score;
    gdouble total;

    if (!result) {
        return -1;
    }
    haystack = g_strjoin(" ", result->title ? result->title : "",
                         result->description ? result->description : "",
                         result->keywords ? result->keywords : "", NULL);
    score = fuzzy_score(query ? query : "", haystack);
    g_free(haystack);
    if (score < 0) {
        return -1;
    }

    total = score + result->base_score;
    if (history && g_strcmp0(history->command, result->command_text) == 0) {
        total += 25.0 + history->uses * 3.0;
    }
    if (target && result->id) {
        if (g_strcmp0(result->id, "float") == 0 && !target->floating) {
            total += 40.0;
        } else if (g_strcmp0(result->id, "tile") == 0 && target->floating) {
            total += 40.0;
        }
    }
    return total;
}

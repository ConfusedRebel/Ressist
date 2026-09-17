#ifndef WINDOWCMD_CONFIG_H
#define WINDOWCMD_CONFIG_H

#include "model.h"

typedef struct {
    gchar *name;
    gchar *command;
    gchar *keywords;
} CustomCommand;

typedef struct {
    gchar *name;
    gchar *command;
} CommandAlias;

typedef struct {
    gdouble exact;
    gdouble prefix;
    gdouble history;
    gdouble frequency;
    gdouble recency;
    gdouble application;
    gdouble state;
} RankingWeights;

typedef struct {
    gint width;
    gint max_results;
    gchar *position;
    gchar *font;
    gint border_radius;
    gdouble transparency;
    guint history_size;
    gchar *shell;
    gboolean include_path_commands;
    guint path_refresh_seconds;
    GPtrArray *disabled_builtins;
    GPtrArray *custom_commands;
    GPtrArray *aliases;
    RankingWeights ranking;
} Config;

void config_set_defaults(Config *config);
gboolean config_load(const gchar *path, Config *out, GError **error);
gboolean config_write_default(const gchar *path, GError **error);
void config_clear(Config *config);
gchar *config_default_path(void);

#endif

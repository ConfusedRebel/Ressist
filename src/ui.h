#ifndef WINDOWCMD_UI_H
#define WINDOWCMD_UI_H

#include "hyprland.h"
#include "providers.h"

typedef struct Ui Ui;

gint ui_selection_move(gint selected, guint count, gint delta);
gboolean ui_result_matches(const CommandResult *result, const gchar *query);
Ui *ui_new(const Config *config, Hyprland *hyprland, HistoryStore *history, GError **error);
void ui_show(Ui *ui, const WindowSnapshot *target);
void ui_hide(Ui *ui);
gboolean ui_is_visible(const Ui *ui);
void ui_refresh_target(Ui *ui);
void ui_free(Ui *ui);

#endif

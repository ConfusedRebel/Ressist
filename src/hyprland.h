#ifndef WINDOWCMD_HYPRLAND_H
#define WINDOWCMD_HYPRLAND_H

#include "model.h"

typedef struct Hyprland Hyprland;

gboolean hyprland_parse_active_json(const gchar *json,
                                    WindowSnapshot *out,
                                    GError **error);
gboolean hyprland_address_in_clients_json(const gchar *json,
                                          const gchar *address,
                                          gboolean *exists,
                                          GError **error);
gboolean hyprland_build_dispatch_argv(const WindowSnapshot *target,
                                      const ParsedAction *action,
                                      gchar ***argv,
                                      GError **error);
gboolean hyprland_capture_active(WindowSnapshot *out, GError **error);
gboolean hyprland_target_exists(const gchar *address, gboolean *exists, GError **error);
gboolean hyprland_dispatch_window(const WindowSnapshot *target,
                                  const ParsedAction *action,
                                  GError **error);
gboolean hyprland_notify(const gchar *message, GError **error);
void hyprland_refresh_capabilities(void);

#endif

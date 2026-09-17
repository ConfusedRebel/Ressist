#ifndef WINDOWCMD_CONTROL_H
#define WINDOWCMD_CONTROL_H

#include "ui.h"

typedef struct {
    Ui *ui;
    gboolean visible;
} AppState;

typedef struct ControlServer ControlServer;

ControlServer *control_server_new(AppState *state, GError **error);
void control_server_free(ControlServer *server);
gboolean control_handle_line(AppState *state, const gchar *line, gchar **response);
gboolean control_send_command(const gchar *command, gchar **response, GError **error);
gchar *control_socket_path(void);

#endif

#include "control.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

#include "hyprland.h"

struct ControlServer {
    GSocketService *service;
    AppState *state;
    gchar *path;
};

gchar *control_socket_path(void)
{
    const gchar *runtime = g_getenv("XDG_RUNTIME_DIR");
    if (!runtime || !*runtime) {
        runtime = g_get_user_runtime_dir();
    }
    return g_build_filename(runtime, "windowcmd.sock", NULL);
}

static void set_response(gchar **response, const gchar *text)
{
    if (response) {
        *response = g_strdup(text);
    }
}

gboolean control_handle_line(AppState *state, const gchar *line, gchar **response)
{
    gchar *command;

    if (!state || !line || !response) {
        return FALSE;
    }
    if (state->ui) {
        state->visible = ui_is_visible(state->ui);
    }
    *response = NULL;
    command = g_strdup(line);
    g_strstrip(command);
    if (g_strcmp0(command, "status") == 0) {
        set_response(response, state->visible ? "visible" : "hidden");
    } else if (g_strcmp0(command, "hide") == 0) {
        if (state->visible) {
            if (state->ui) {
                ui_hide(state->ui);
            }
            state->visible = FALSE;
            set_response(response, "hidden");
        } else {
            set_response(response, "already hidden");
        }
    } else if (g_strcmp0(command, "show") == 0) {
        if (state->visible) {
            set_response(response, "already visible");
        } else if (state->ui) {
            WindowSnapshot target = {0};
            GError *error = NULL;
            if (!hyprland_capture_active(&target, &error)) {
                set_response(response, error ? error->message : "error: no active Hyprland window");
                g_clear_error(&error);
                g_free(command);
                return FALSE;
            }
            ui_show(state->ui, &target);
            state->visible = ui_is_visible(state->ui);
            window_snapshot_clear(&target);
            set_response(response, state->visible ? "shown" : "error: palette did not open");
        } else {
            state->visible = TRUE;
            set_response(response, "shown");
        }
    } else if (g_strcmp0(command, "toggle") == 0) {
        if (state->visible) {
            if (state->ui) {
                ui_hide(state->ui);
            }
            state->visible = FALSE;
            set_response(response, "hidden");
        } else {
            g_free(command);
            return control_handle_line(state, "show", response);
        }
    } else {
        set_response(response, "error: unknown command");
        g_free(command);
        return FALSE;
    }
    g_free(command);
    return TRUE;
}

static gboolean incoming(GSocketService *service,
                         GSocketConnection *connection,
                         GObject *source_object,
                         gpointer user_data)
{
    ControlServer *server = user_data;
    GInputStream *input = g_io_stream_get_input_stream(G_IO_STREAM(connection));
    GOutputStream *output = g_io_stream_get_output_stream(G_IO_STREAM(connection));
    gchar buffer[512] = {0};
    gssize length;
    gchar *response = NULL;
    GError *error = NULL;
    (void) service;
    (void) source_object;

    length = g_input_stream_read(input, buffer, sizeof(buffer) - 1, NULL, &error);
    if (length > 0) {
        buffer[length] = '\0';
        control_handle_line(server->state, buffer, &response);
        g_output_stream_write_all(output, response ? response : "error: no response", strlen(response ? response : "error: no response"), NULL, NULL, &error);
        g_output_stream_write_all(output, "\n", 1, NULL, NULL, &error);
    }
    g_clear_error(&error);
    g_free(response);
    return TRUE;
}

ControlServer *control_server_new(AppState *state, GError **error)
{
    ControlServer *server;
    GSocketAddress *address;
    gchar *path;

    if (!state) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "application state is missing");
        return NULL;
    }
    path = control_socket_path();
    server = g_new0(ControlServer, 1);
    server->state = state;
    server->path = path;
    server->service = g_socket_service_new();
    address = G_SOCKET_ADDRESS(g_unix_socket_address_new(path));
    if (!g_socket_listener_add_address(G_SOCKET_LISTENER(server->service),
                                       address,
                                       G_SOCKET_TYPE_STREAM,
                                       G_SOCKET_PROTOCOL_DEFAULT,
                                       NULL, NULL, error)) {
        g_object_unref(address);
        control_server_free(server);
        return NULL;
    }
    g_chmod(path, 0600);
    g_signal_connect(server->service, "incoming", G_CALLBACK(incoming), server);
    g_socket_service_start(server->service);
    g_object_unref(address);
    return server;
}

void control_server_free(ControlServer *server)
{
    if (!server) {
        return;
    }
    if (server->service) {
        g_socket_service_stop(server->service);
        g_object_unref(server->service);
    }
    if (server->path) {
        g_remove(server->path);
    }
    g_free(server->path);
    g_free(server);
}

gboolean control_send_command(const gchar *command, gchar **response, GError **error)
{
    GSocketClient *client;
    GSocketConnection *connection;
    GSocketAddress *address;
    GDataOutputStream *output;
    GDataInputStream *input;
    gchar *path;
    gchar *line;

    if (!command || !response) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "command is missing");
        return FALSE;
    }
    path = control_socket_path();
    client = g_socket_client_new();
    address = G_SOCKET_ADDRESS(g_unix_socket_address_new(path));
    connection = g_socket_client_connect(client, G_SOCKET_CONNECTABLE(address), NULL, error);
    if (!connection) {
        g_object_unref(address);
        g_object_unref(client);
        g_free(path);
        return FALSE;
    }
    output = g_data_output_stream_new(g_io_stream_get_output_stream(G_IO_STREAM(connection)));
    input = g_data_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(connection)));
    g_data_output_stream_put_string(output, command, NULL, error);
    g_data_output_stream_put_byte(output, '\n', NULL, error);
    line = g_data_input_stream_read_line(input, NULL, NULL, error);
    *response = line;
    g_object_unref(input);
    g_object_unref(output);
    g_object_unref(connection);
    g_object_unref(address);
    g_object_unref(client);
    g_free(path);
    return line != NULL && (!error || !*error);
}

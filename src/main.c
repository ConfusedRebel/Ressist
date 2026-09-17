#include <gio/gio.h>
#include <glib-unix.h>
#include <gtk/gtk.h>

#include <signal.h>
#include <stdio.h>

#include "config.h"
#include "control.h"
#include "history.h"
#include "ui.h"

typedef struct {
    Config config;
    HistoryStore *history;
    gchar *history_path;
    Ui *ui;
    ControlServer *server;
    GMainLoop *loop;
} Runtime;

static void print_help(void)
{
    puts("windowcmd - Hyprland command palette");
    puts("");
    puts("Usage:");
    puts("  windowcmd start       Run the background service");
    puts("  windowcmd toggle      Show or hide the palette");
    puts("  windowcmd show        Show the palette for the focused window");
    puts("  windowcmd hide        Hide the palette");
    puts("  windowcmd status      Print service visibility");
    puts("  windowcmd init        Create the default config file");
}

static gboolean quit_loop(gpointer data)
{
    Runtime *runtime = data;
    g_main_loop_quit(runtime->loop);
    return G_SOURCE_REMOVE;
}

static void runtime_clear(Runtime *runtime)
{
    GError *error = NULL;
    if (runtime->history && runtime->history_path &&
        !history_save(runtime->history, runtime->history_path, &error)) {
        g_warning("windowcmd: cannot save history: %s", error ? error->message : "unknown error");
        g_clear_error(&error);
    }
    control_server_free(runtime->server);
    ui_free(runtime->ui);
    history_store_free(runtime->history);
    config_clear(&runtime->config);
    g_free(runtime->history_path);
    if (runtime->loop) {
        g_main_loop_unref(runtime->loop);
    }
}

static int run_daemon(void)
{
    Runtime runtime = {0};
    gchar *config_path;
    GError *error = NULL;
    int result = 1;

    gtk_init();
    config_path = config_default_path();
    if (!config_load(config_path, &runtime.config, &error)) {
        g_warning("windowcmd: cannot load config: %s", error ? error->message : "unknown error");
        g_clear_error(&error);
    }
    g_free(config_path);
    runtime.history_path = history_default_path();
    if (!history_load(runtime.history_path, runtime.config.history_size, &runtime.history, &error)) {
        g_warning("windowcmd: cannot load history: %s", error ? error->message : "unknown error");
        g_clear_error(&error);
        history_store_free(runtime.history);
        runtime.history = NULL;
        history_load(NULL, runtime.config.history_size, &runtime.history, NULL);
    }
    runtime.ui = ui_new(&runtime.config, NULL, runtime.history, &error);
    if (!runtime.ui) {
        fprintf(stderr, "windowcmd: cannot create Wayland palette: %s\n", error ? error->message : "unknown error");
        g_clear_error(&error);
        runtime_clear(&runtime);
        return result;
    }
    {
        AppState state = {.ui = runtime.ui, .visible = FALSE};
        runtime.server = control_server_new(&state, &error);
        if (!runtime.server) {
            fprintf(stderr, "windowcmd: cannot start service: %s\n", error ? error->message : "unknown error");
            g_clear_error(&error);
            runtime_clear(&runtime);
            return result;
        }
        runtime.loop = g_main_loop_new(NULL, FALSE);
        g_unix_signal_add(SIGINT, quit_loop, &runtime);
        g_unix_signal_add(SIGTERM, quit_loop, &runtime);
        puts("windowcmd: running");
        fflush(stdout);
        g_main_loop_run(runtime.loop);
    }
    runtime_clear(&runtime);
    result = 0;
    return result;
}

static int run_client(const gchar *command)
{
    gchar *response = NULL;
    GError *error = NULL;
    gboolean ok = control_send_command(command, &response, &error);
    if (!ok) {
        fprintf(stderr, "windowcmd: service is not running (%s)\n",
                error ? error->message : "no response");
        g_clear_error(&error);
        return 1;
    }
    puts(response ? response : "");
    g_free(response);
    return 0;
}

int app_run(int argc, char **argv)
{
    const gchar *command = argc > 1 ? argv[1] : "start";
    if (g_strcmp0(command, "--help") == 0 || g_strcmp0(command, "-h") == 0) {
        print_help();
        return 0;
    }
    if (g_strcmp0(command, "start") == 0) {
        return run_daemon();
    }
    if (g_strcmp0(command, "init") == 0) {
        gchar *path = config_default_path();
        GError *error = NULL;
        gboolean ok = config_write_default(path, &error);
        if (ok) {
            printf("config: %s\n", path);
        } else {
            fprintf(stderr, "windowcmd: %s\n", error ? error->message : "cannot create config");
        }
        g_clear_error(&error);
        g_free(path);
        return ok ? 0 : 1;
    }
    if (g_strcmp0(command, "toggle") == 0 || g_strcmp0(command, "show") == 0 ||
        g_strcmp0(command, "hide") == 0 || g_strcmp0(command, "status") == 0) {
        return run_client(command);
    }
    print_help();
    return 2;
}

int main(int argc, char **argv)
{
    return app_run(argc, argv);
}

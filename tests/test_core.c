#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "model.h"
#include "parser.h"
#include "ranking.h"
#include "config.h"
#include "history.h"
#include "hyprland.h"

static WindowSnapshot test_target(void)
{
    WindowSnapshot target = {
        .address = g_strdup("0x1234"),
        .class_name = g_strdup("Firefox"),
        .title = g_strdup("Browser"),
        .pid = 42,
        .workspace = 2,
        .workspace_name = g_strdup("2"),
        .monitor = 0,
        .monitor_name = g_strdup("DP-1"),
        .floating = FALSE,
        .fullscreen = FALSE,
        .pinned = FALSE,
        .pseudo = FALSE,
        .split_locked = FALSE,
    };
    return target;
}

static void test_parse_workspace(void)
{
    WindowSnapshot target = test_target();
    ParsedAction action = {0};
    GError *error = NULL;

    g_assert_true(parse_window_command("workspace 4", &target, &action, &error));
    g_assert_no_error(error);
    g_assert_cmpint(action.kind, ==, ACTION_WORKSPACE);
    g_assert_cmpstr(action.workspace, ==, "4");

    window_snapshot_clear(&target);
}

static void test_parse_silent_workspace(void)
{
    WindowSnapshot target = test_target();
    ParsedAction action = {0};
    GError *error = NULL;

    g_assert_true(parse_window_command("move workspace 4", &target, &action, &error));
    g_assert_no_error(error);
    g_assert_cmpint(action.kind, ==, ACTION_MOVE_WORKSPACE_SILENT);
    g_assert_cmpstr(action.workspace, ==, "4");

    window_snapshot_clear(&target);
}

static void test_parse_resize_and_opacity(void)
{
    WindowSnapshot target = test_target();
    ParsedAction action = {0};
    GError *error = NULL;

    g_assert_true(parse_window_command("resize 1200 800", &target, &action, &error));
    g_assert_no_error(error);
    g_assert_cmpint(action.kind, ==, ACTION_RESIZE);
    g_assert_cmpint(action.width, ==, 1200);
    g_assert_cmpint(action.height, ==, 800);

    g_assert_true(parse_window_command("opacity 0.9", &target, &action, &error));
    g_assert_no_error(error);
    g_assert_cmpint(action.kind, ==, ACTION_OPACITY);
    g_assert_cmpfloat(action.opacity, ==, 0.9);

    window_snapshot_clear(&target);
}

static void test_parse_rejects_invalid_arguments(void)
{
    WindowSnapshot target = test_target();
    ParsedAction action = {0};
    GError *error = NULL;

    g_assert_false(parse_window_command("resize 1200", &target, &action, &error));
    g_assert_error(error, WINDOWCMD_ERROR, WINDOWCMD_ERROR_INVALID_ARGUMENT);
    g_clear_error(&error);

    g_assert_false(parse_window_command("opacity 1.2", &target, &action, &error));
    g_assert_error(error, WINDOWCMD_ERROR, WINDOWCMD_ERROR_INVALID_ARGUMENT);
    g_clear_error(&error);

    window_snapshot_clear(&target);
}

static void test_fuzzy_prefix_beats_scattered_match(void)
{
    g_assert_cmpint(fuzzy_score("flo", "float"), >, fuzzy_score("flo", "file-open"));
    g_assert_cmpint(fuzzy_score("float", "float"), >, fuzzy_score("float", "floating"));
    g_assert_cmpint(fuzzy_score("xyz", "float"), ==, -1);
}

static void test_config_defaults_and_overrides(void)
{
    gchar *directory = g_dir_make_tmp("windowcmd-config-XXXXXX", NULL);
    gchar *path = g_build_filename(directory, "config.json", NULL);
    const gchar *json = "{\"width\": 720, \"shell\": \"/bin/sh\", "
                        "\"custom_commands\": [{\"name\": 42}]}";
    Config config = {0};
    GError *error = NULL;

    g_assert_true(config_load(NULL, &config, &error));
    g_assert_no_error(error);
    g_assert_cmpint(config.width, ==, 640);
    config_clear(&config);

    g_assert_true(g_file_set_contents(path, json, -1, &error));
    g_assert_no_error(error);
    g_assert_true(config_load(path, &config, &error));
    g_assert_no_error(error);
    g_assert_cmpint(config.width, ==, 720);
    g_assert_cmpstr(config.shell, ==, "/bin/sh");
    g_assert_cmpuint(config.custom_commands->len, ==, 0);

    config_clear(&config);
    g_remove(path);
    g_rmdir(directory);
    g_free(path);
    g_free(directory);
}

static void test_history_counts_and_trims(void)
{
    gchar *directory = g_dir_make_tmp("windowcmd-history-XXXXXX", NULL);
    gchar *path = g_build_filename(directory, "history.json", NULL);
    HistoryStore *history = NULL;
    GError *error = NULL;
    WindowSnapshot target = test_target();

    g_assert_true(history_load(path, 2, &history, &error));
    g_assert_no_error(error);
    history_record(history, "float", COMMAND_WINDOW, target.class_name, 100);
    history_record(history, "float", COMMAND_WINDOW, target.class_name, 101);
    history_record(history, "workspace 4", COMMAND_WINDOW, target.class_name, 102);
    history_record(history, "printf hi", COMMAND_SHELL, target.class_name, 103);
    g_assert_cmpuint(history->entries->len, ==, 2);
    g_assert_true(history_save(history, path, &error));
    g_assert_no_error(error);

    history_store_free(history);
    history = NULL;
    g_assert_true(history_load(path, 2, &history, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(history->entries->len, ==, 2);
    g_assert_cmpuint(((HistoryEntry *) g_ptr_array_index(history->entries, 0))->uses, ==, 1);

    history_store_free(history);
    window_snapshot_clear(&target);
    g_remove(path);
    g_rmdir(directory);
    g_free(path);
    g_free(directory);
}

static void test_hyprland_fixture_parsing(void)
{
    gchar *json = NULL;
    WindowSnapshot snapshot = {0};
    gboolean exists = FALSE;
    GError *error = NULL;

    g_assert_true(g_file_get_contents("tests/fixtures/activewindow.json", &json, NULL, &error));
    g_assert_no_error(error);
    g_assert_true(hyprland_parse_active_json(json, &snapshot, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(snapshot.address, ==, "0x1234");
    g_assert_cmpstr(snapshot.class_name, ==, "Firefox");
    g_assert_cmpstr(snapshot.title, ==, "YouTube; \"quoted\"");
    g_assert_cmpint(snapshot.pid, ==, 42);
    g_assert_cmpint(snapshot.workspace, ==, 2);
    g_assert_cmpint(snapshot.monitor, ==, 0);
    g_assert_false(snapshot.floating);
    g_assert_true(snapshot.pinned);
    g_assert_true(snapshot.fullscreen);

    g_assert_true(g_file_get_contents("tests/fixtures/clients.json", &json, NULL, &error));
    g_assert_no_error(error);
    g_assert_true(hyprland_address_in_clients_json(json, "0x1234", &exists, &error));
    g_assert_no_error(error);
    g_assert_true(exists);
    g_assert_true(hyprland_address_in_clients_json(json, "0x9999", &exists, &error));
    g_assert_no_error(error);
    g_assert_false(exists);

    window_snapshot_clear(&snapshot);
    g_free(json);
}

static void test_hyprland_dispatch_does_not_include_title(void)
{
    WindowSnapshot snapshot = {
        .address = g_strdup("0x1234"),
        .title = g_strdup("do not execute; rm -rf /")
    };
    ParsedAction action = {.kind = ACTION_CLOSE};
    gchar **argv = NULL;
    gchar *joined;
    GError *error = NULL;

    g_assert_true(hyprland_build_dispatch_argv(&snapshot, &action, &argv, &error));
    g_assert_no_error(error);
    joined = g_strjoinv(" ", argv);
    g_assert_nonnull(strstr(joined, "address:0x1234"));
    g_assert_null(strstr(joined, snapshot.title));

    g_free(joined);
    g_strfreev(argv);
    window_snapshot_clear(&snapshot);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/parser/workspace", test_parse_workspace);
    g_test_add_func("/parser/silent-workspace", test_parse_silent_workspace);
    g_test_add_func("/parser/resize-opacity", test_parse_resize_and_opacity);
    g_test_add_func("/parser/invalid-arguments", test_parse_rejects_invalid_arguments);
    g_test_add_func("/ranking/fuzzy-prefix", test_fuzzy_prefix_beats_scattered_match);
    g_test_add_func("/config/defaults-overrides", test_config_defaults_and_overrides);
    g_test_add_func("/history/counts-trims", test_history_counts_and_trims);
    g_test_add_func("/hyprland/fixture-parsing", test_hyprland_fixture_parsing);
    g_test_add_func("/hyprland/safe-dispatch", test_hyprland_dispatch_does_not_include_title);
    return g_test_run();
}

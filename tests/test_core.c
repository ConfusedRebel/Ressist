#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "model.h"
#include "parser.h"
#include "ranking.h"
#include "config.h"
#include "history.h"
#include "hyprland.h"
#include "providers.h"
#include "ui.h"

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

static void test_parse_all_displayed_window_actions(void)
{
    WindowSnapshot target = test_target();
    ParsedAction action = {0};
    GError *error = NULL;

    g_assert_true(parse_window_command("pin", &target, &action, &error));
    g_assert_cmpint(action.kind, ==, ACTION_PIN);
    g_assert_true(parse_window_command("unpin", &target, &action, &error));
    g_assert_cmpint(action.kind, ==, ACTION_UNPIN);
    g_assert_true(parse_window_command("center", &target, &action, &error));
    g_assert_cmpint(action.kind, ==, ACTION_CENTER);
    g_assert_true(parse_window_command("move left", &target, &action, &error));
    g_assert_cmpint(action.kind, ==, ACTION_MOVE_DIRECTION);
    g_assert_cmpstr(action.direction, ==, "l");
    g_assert_true(parse_window_command("pseudo", &target, &action, &error));
    g_assert_cmpint(action.kind, ==, ACTION_TOGGLE_PSEUDO);
    g_assert_true(parse_window_command("split", &target, &action, &error));
    g_assert_cmpint(action.kind, ==, ACTION_TOGGLE_SPLIT);
    g_assert_true(parse_window_command("monitor HDMI-A-1", &target, &action, &error));
    g_assert_cmpint(action.kind, ==, ACTION_MONITOR);
    g_assert_cmpstr(action.monitor, ==, "HDMI-A-1");
    g_assert_no_error(error);

    parsed_action_clear(&action);
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

static void test_config_init_writes_example_once(void)
{
    gchar *directory = g_dir_make_tmp("windowcmd-init-XXXXXX", NULL);
    gchar *path = g_build_filename(directory, "config.json", NULL);
    Config config = {0};
    GError *error = NULL;

    g_assert_true(config_write_default(path, &error));
    g_assert_no_error(error);
    g_assert_true(config_load(path, &config, &error));
    g_assert_no_error(error);
    g_assert_cmpint(config.width, ==, 640);
    config_clear(&config);
    g_assert_true(g_file_set_contents(path, "{\"width\": 720}", -1, &error));
    g_assert_true(config_write_default(path, &error));
    g_assert_true(config_load(path, &config, &error));
    g_assert_cmpint(config.width, ==, 720);

    config_clear(&config);
    g_remove(path);
    g_rmdir(directory);
    g_free(path);
    g_free(directory);
}

static void test_history_default_path(void)
{
    gchar *path = history_default_path();
    g_assert_true(g_str_has_suffix(path, "windowcmd/history.json"));
    g_free(path);
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

static void test_hyprland_opacity_dispatch_is_structured(void)
{
    WindowSnapshot snapshot = {.address = g_strdup("0x1234")};
    ParsedAction action = {.kind = ACTION_OPACITY, .opacity = 0.9};
    gchar **argv = NULL;
    GError *error = NULL;

    g_assert_true(hyprland_build_dispatch_argv(&snapshot, &action, &argv, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(argv[1], ==, "setprop");
    g_assert_cmpstr(argv[2], ==, "address:0x1234");
    g_assert_cmpstr(argv[3], ==, "opacity");
    g_assert_cmpstr(argv[4], ==, "0.9");
    g_assert_null(argv[5]);

    g_strfreev(argv);
    window_snapshot_clear(&snapshot);
}

static CommandResult *find_result(GPtrArray *results, const gchar *id)
{
    for (guint i = 0; i < results->len; i++) {
        CommandResult *result = g_ptr_array_index(results, i);
        if (g_strcmp0(result->id, id) == 0) {
            return result;
        }
    }
    return NULL;
}

static void test_provider_context_and_disabled_actions(void)
{
    Config config = {0};
    WindowSnapshot target = test_target();
    GPtrArray *results = NULL;
    GError *error = NULL;
    CommandResult *result;

    config_set_defaults(&config);
    config.include_path_commands = FALSE;
    g_assert_true(provider_collect(&config, &target, NULL, &results, &error));
    g_assert_no_error(error);
    result = find_result(results, "float");
    g_assert_nonnull(result);
    g_assert_cmpstr(result->title, ==, "FLOAT");
    g_ptr_array_unref(results);

    target.floating = TRUE;
    g_assert_true(provider_collect(&config, &target, NULL, &results, &error));
    result = find_result(results, "float");
    g_assert_nonnull(result);
    g_assert_cmpstr(result->title, ==, "TILE");
    g_ptr_array_unref(results);

    g_ptr_array_add(config.disabled_builtins, g_strdup("close"));
    g_assert_true(provider_collect(&config, &target, NULL, &results, &error));
    g_assert_null(find_result(results, "close"));
    g_ptr_array_unref(results);
    config_clear(&config);
    window_snapshot_clear(&target);
}

static void test_path_cache_and_target_template(void)
{
    gchar *directory = g_dir_make_tmp("windowcmd-path-XXXXXX", NULL);
    gchar *executable = g_build_filename(directory, "windowcmd-test", NULL);
    const gchar *old_path = g_getenv("PATH");
    PathCache cache = {0};
    WindowSnapshot target = test_target();
    gchar *expanded = NULL;
    GError *error = NULL;

    g_assert_true(g_file_set_contents(executable, "#!/bin/sh\n", -1, &error));
    g_assert_no_error(error);
    g_assert_cmpint(g_chmod(executable, 0700), ==, 0);
    g_setenv("PATH", directory, TRUE);
    g_assert_true(path_cache_refresh_if_needed(&cache, directory, 600, &error));
    g_assert_no_error(error);
    g_assert_true(g_hash_table_contains(cache.commands, "windowcmd-test"));
    g_assert_true(expand_target_template("pid=${HYPR_TARGET_PID} title=${HYPR_TARGET_TITLE}",
                                         &target, &expanded, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(expanded, ==, "pid=42 title=Browser");

    g_setenv("PATH", old_path ? old_path : "", TRUE);
    g_free(expanded);
    path_cache_clear(&cache);
    window_snapshot_clear(&target);
    g_remove(executable);
    g_rmdir(directory);
    g_free(executable);
    g_free(directory);
}

static void test_path_provider_is_shell_kind(void)
{
    gchar *directory = g_dir_make_tmp("windowcmd-provider-XXXXXX", NULL);
    gchar *executable = g_build_filename(directory, "windowcmd-test", NULL);
    const gchar *old_path = g_getenv("PATH");
    Config config = {0};
    WindowSnapshot target = test_target();
    GPtrArray *results = NULL;
    gboolean found_shell = FALSE;
    GError *error = NULL;

    g_file_set_contents(executable, "#!/bin/sh\n", -1, NULL);
    g_chmod(executable, 0700);
    g_setenv("PATH", directory, TRUE);
    config_set_defaults(&config);
    g_assert_true(provider_collect(&config, &target, NULL, &results, &error));
    g_assert_no_error(error);
    for (guint i = 0; i < results->len; i++) {
        CommandResult *result = g_ptr_array_index(results, i);
        if (g_strcmp0(result->id, "windowcmd-test") == 0) {
            g_assert_cmpint(result->kind, ==, COMMAND_SHELL);
            found_shell = TRUE;
        }
    }
    g_assert_true(found_shell);

    g_setenv("PATH", old_path ? old_path : "", TRUE);
    g_ptr_array_unref(results);
    config_clear(&config);
    window_snapshot_clear(&target);
    g_remove(executable);
    g_rmdir(directory);
    g_free(executable);
    g_free(directory);
}

static void test_arbitrary_query_becomes_shell_result(void)
{
    CommandResult *result = provider_shell_result("echo hello");
    g_assert_nonnull(result);
    g_assert_cmpint(result->kind, ==, COMMAND_SHELL);
    g_assert_cmpstr(result->command_text, ==, "echo hello");
    g_assert_true(g_str_has_prefix(result->title, "RUN"));
    command_result_free(result);
}

static void test_ui_navigation_and_filtering(void)
{
    CommandResult result = {
        .title = "Toggle Floating",
        .description = "Float the window",
        .keywords = "float tile"
    };

    g_assert_cmpint(ui_selection_move(0, 3, -1), ==, 2);
    g_assert_cmpint(ui_selection_move(2, 3, 1), ==, 0);
    g_assert_cmpint(ui_selection_move(0, 0, 1), ==, -1);
    g_assert_true(ui_result_matches(&result, "flt"));
    g_assert_false(ui_result_matches(&result, "xyz"));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/parser/workspace", test_parse_workspace);
    g_test_add_func("/parser/silent-workspace", test_parse_silent_workspace);
    g_test_add_func("/parser/resize-opacity", test_parse_resize_and_opacity);
    g_test_add_func("/parser/invalid-arguments", test_parse_rejects_invalid_arguments);
    g_test_add_func("/parser/displayed-actions", test_parse_all_displayed_window_actions);
    g_test_add_func("/ranking/fuzzy-prefix", test_fuzzy_prefix_beats_scattered_match);
    g_test_add_func("/config/defaults-overrides", test_config_defaults_and_overrides);
    g_test_add_func("/history/counts-trims", test_history_counts_and_trims);
    g_test_add_func("/config/init-once", test_config_init_writes_example_once);
    g_test_add_func("/history/default-path", test_history_default_path);
    g_test_add_func("/hyprland/fixture-parsing", test_hyprland_fixture_parsing);
    g_test_add_func("/hyprland/safe-dispatch", test_hyprland_dispatch_does_not_include_title);
    g_test_add_func("/hyprland/opacity-dispatch", test_hyprland_opacity_dispatch_is_structured);
    g_test_add_func("/providers/context-disabled", test_provider_context_and_disabled_actions);
    g_test_add_func("/providers/path-template", test_path_cache_and_target_template);
    g_test_add_func("/providers/path-shell-kind", test_path_provider_is_shell_kind);
    g_test_add_func("/providers/arbitrary-shell", test_arbitrary_query_becomes_shell_result);
    g_test_add_func("/ui/navigation-filtering", test_ui_navigation_and_filtering);
    return g_test_run();
}

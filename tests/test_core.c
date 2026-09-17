#include <glib.h>

#include "model.h"
#include "parser.h"
#include "ranking.h"

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

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/parser/workspace", test_parse_workspace);
    g_test_add_func("/parser/silent-workspace", test_parse_silent_workspace);
    g_test_add_func("/parser/resize-opacity", test_parse_resize_and_opacity);
    g_test_add_func("/parser/invalid-arguments", test_parse_rejects_invalid_arguments);
    g_test_add_func("/ranking/fuzzy-prefix", test_fuzzy_prefix_beats_scattered_match);
    return g_test_run();
}

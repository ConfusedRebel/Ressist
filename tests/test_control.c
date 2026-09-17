#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "control.h"

static void test_control_commands(void)
{
    AppState state = {0};
    gchar *response = NULL;

    g_assert_true(control_handle_line(&state, "status", &response));
    g_assert_cmpstr(response, ==, "hidden");
    g_free(response);

    g_assert_true(control_handle_line(&state, "show", &response));
    g_assert_cmpstr(response, ==, "shown");
    g_free(response);
    g_assert_true(state.visible);

    g_assert_true(control_handle_line(&state, "show", &response));
    g_assert_cmpstr(response, ==, "already visible");
    g_free(response);

    g_assert_true(control_handle_line(&state, "toggle", &response));
    g_assert_cmpstr(response, ==, "hidden");
    g_free(response);
    g_assert_false(state.visible);

    g_assert_true(control_handle_line(&state, "hide", &response));
    g_assert_cmpstr(response, ==, "already hidden");
    g_free(response);
}

static void test_control_rejects_unknown_command(void)
{
    AppState state = {0};
    gchar *response = NULL;

    g_assert_false(control_handle_line(&state, "launch", &response));
    g_assert_nonnull(response);
    g_assert_true(g_str_has_prefix(response, "error:"));
    g_free(response);
}

static void test_control_is_single_instance(void)
{
    gchar *runtime = g_dir_make_tmp("windowcmd-runtime-XXXXXX", NULL);
    const gchar *old_runtime = g_getenv("XDG_RUNTIME_DIR");
    AppState state = {0};
    ControlServer *first;
    ControlServer *second;
    GError *error = NULL;

    g_setenv("XDG_RUNTIME_DIR", runtime, TRUE);
    first = control_server_new(&state, &error);
    g_assert_no_error(error);
    g_assert_nonnull(first);
    second = control_server_new(&state, &error);
    g_assert_null(second);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_ADDRESS_IN_USE);
    g_clear_error(&error);
    control_server_free(first);
    g_setenv("XDG_RUNTIME_DIR", old_runtime ? old_runtime : "", TRUE);
    g_rmdir(runtime);
    g_free(runtime);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/control/commands", test_control_commands);
    g_test_add_func("/control/unknown", test_control_rejects_unknown_command);
    g_test_add_func("/control/single-instance", test_control_is_single_instance);
    return g_test_run();
}

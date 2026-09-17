#include "ui.h"

#include <gtk/gtk.h>
#include <gtk4-layer-shell/gtk4-layer-shell.h>

#include <string.h>

#include "ranking.h"

typedef struct {
    CommandResult *result;
    gdouble score;
} RankedResult;

struct Ui {
    const Config *config;
    Hyprland *hyprland;
    HistoryStore *history;
    GtkWindow *window;
    GtkEntry *entry;
    GtkLabel *ghost_label;
    GtkLabel *current_description;
    GtkListBox *list;
    GtkLabel *target_label;
    GtkLabel *status_label;
    GPtrArray *results;
    GPtrArray *visible;
    CommandResult *typed_result;
    WindowSnapshot target;
    gint selected;
    guint target_timer;
    gboolean visible_now;
};

static void snapshot_copy(WindowSnapshot *destination, const WindowSnapshot *source)
{
    window_snapshot_clear(destination);
    destination->address = g_strdup(source->address);
    destination->class_name = g_strdup(source->class_name);
    destination->title = g_strdup(source->title);
    destination->pid = source->pid;
    destination->workspace = source->workspace;
    destination->workspace_name = g_strdup(source->workspace_name);
    destination->monitor = source->monitor;
    destination->monitor_name = g_strdup(source->monitor_name);
    destination->floating = source->floating;
    destination->fullscreen = source->fullscreen;
    destination->pinned = source->pinned;
    destination->pseudo = source->pseudo;
    destination->split_locked = source->split_locked;
}

static void ranked_free(gpointer data)
{
    g_free(data);
}

gint ui_selection_move(gint selected, guint count, gint delta)
{
    gint next;
    if (count == 0) {
        return -1;
    }
    next = selected < 0 ? 0 : selected + delta;
    while (next < 0) {
        next += count;
    }
    return next % (gint) count;
}

gboolean ui_result_matches(const CommandResult *result, const gchar *query)
{
    gchar *haystack;
    gboolean matches;
    if (!result) {
        return FALSE;
    }
    haystack = g_strjoin(" ", result->title ? result->title : "",
                         result->description ? result->description : "",
                         result->keywords ? result->keywords : "", NULL);
    matches = fuzzy_score(query ? query : "", haystack) >= 0;
    g_free(haystack);
    return matches;
}

gchar *ui_completion_for_query(const CommandResult *result, const gchar *query)
{
    gsize query_length;
    if (!result || !result->command_text || !query || !*query) {
        return NULL;
    }
    query_length = strlen(query);
    if (strlen(result->command_text) <= query_length ||
        g_ascii_strncasecmp(result->command_text, query, query_length) != 0) {
        return NULL;
    }
    return g_strdup(result->command_text);
}

static gint ranked_compare(gconstpointer left, gconstpointer right, gpointer user_data)
{
    const RankedResult *a = *(RankedResult *const *) left;
    const RankedResult *b = *(RankedResult *const *) right;
    (void) user_data;
    if (a->score < b->score) return 1;
    if (a->score > b->score) return -1;
    return g_ascii_strcasecmp(a->result->title, b->result->title);
}

static HistoryEntry *find_history(const Ui *ui, const CommandResult *result)
{
    if (!ui->history || !result->command_text) {
        return NULL;
    }
    for (guint i = 0; i < ui->history->entries->len; i++) {
        HistoryEntry *entry = g_ptr_array_index(ui->history->entries, i);
        if (entry->kind == result->kind && g_strcmp0(entry->command, result->command_text) == 0) {
            return entry;
        }
    }
    return NULL;
}

static void clear_rows(Ui *ui)
{
    GtkListBoxRow *row;
    while ((row = gtk_list_box_get_row_at_index(ui->list, 0))) {
        gtk_list_box_remove(ui->list, GTK_WIDGET(row));
    }
}

static GtkWidget *make_row(CommandResult *result)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *title = gtk_label_new(result->title);
    GtkWidget *description = gtk_label_new(result->description);
    GtkWidget *provider = gtk_label_new(result->provider);
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_label_set_xalign(GTK_LABEL(description), 0);
    gtk_label_set_xalign(GTK_LABEL(provider), 0);
    gtk_label_set_ellipsize(GTK_LABEL(description), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(title, "result-title");
    gtk_widget_add_css_class(description, "result-description");
    gtk_widget_add_css_class(provider, "result-provider");
    gtk_box_append(GTK_BOX(box), title);
    gtk_box_append(GTK_BOX(box), description);
    gtk_box_append(GTK_BOX(box), provider);
    return box;
}

static void refresh_rows(Ui *ui)
{
    const gchar *query = gtk_editable_get_text(GTK_EDITABLE(ui->entry));
    GPtrArray *ranked = g_ptr_array_new_with_free_func(ranked_free);

    if (ui->typed_result) {
        g_ptr_array_remove(ui->results, ui->typed_result);
        ui->typed_result = NULL;
    }
    if (query && strchr(query, ' ')) {
        ui->typed_result = provider_shell_result(query);
        if (ui->typed_result) {
            g_ptr_array_add(ui->results, ui->typed_result);
        }
    }

    for (guint i = 0; ui->results && i < ui->results->len; i++) {
        CommandResult *result = g_ptr_array_index(ui->results, i);
        RankedResult *ranked_result;
        if (!ui_result_matches(result, query)) {
            continue;
        }
        ranked_result = g_new0(RankedResult, 1);
        ranked_result->result = result;
        ranked_result->score = rank_result(result, query, &ui->target, find_history(ui, result));
        g_ptr_array_add(ranked, ranked_result);
    }
    g_ptr_array_sort_with_data(ranked, ranked_compare, NULL);
    clear_rows(ui);
    g_clear_pointer(&ui->visible, g_ptr_array_unref);
    ui->visible = g_ptr_array_new();
    ui->selected = -1;
    for (guint i = 0; i < ranked->len && (gint) i < ui->config->max_results; i++) {
        RankedResult *ranked_result = g_ptr_array_index(ranked, i);
        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), make_row(ranked_result->result));
        gtk_list_box_append(ui->list, row);
        g_ptr_array_add(ui->visible, ranked_result->result);
    }
    if (ui->visible->len > 0) {
        ui->selected = 0;
        gtk_widget_add_css_class(GTK_WIDGET(gtk_list_box_get_row_at_index(ui->list, 0)), "selected");
    }
    gtk_label_set_text(ui->status_label, ui->visible->len ? "" : "No matching commands");
    if (ui->visible->len > 0) {
        CommandResult *selected = g_ptr_array_index(ui->visible, 0);
        gchar *completion = ui_completion_for_query(selected, query);
        gtk_label_set_text(ui->current_description, selected->description);
        gtk_label_set_text(ui->ghost_label, completion ? completion : "");
        g_free(completion);
    } else {
        gtk_label_set_text(ui->current_description, "");
        gtk_label_set_text(ui->ghost_label, "");
    }
    g_ptr_array_unref(ranked);
}

static void set_selected(Ui *ui, gint selected)
{
    GtkListBoxRow *old_row;
    GtkListBoxRow *new_row;
    if (!ui->visible || ui->visible->len == 0) {
        ui->selected = -1;
        return;
    }
    old_row = gtk_list_box_get_row_at_index(ui->list, ui->selected);
    if (old_row) {
        gtk_widget_remove_css_class(GTK_WIDGET(old_row), "selected");
    }
    ui->selected = selected;
    new_row = gtk_list_box_get_row_at_index(ui->list, ui->selected);
    if (new_row) {
        gtk_widget_add_css_class(GTK_WIDGET(new_row), "selected");
    }
    if (ui->current_description && ui->visible && ui->selected >= 0) {
        CommandResult *result = g_ptr_array_index(ui->visible, ui->selected);
        gtk_label_set_text(ui->current_description, result->description);
    }
}

static void execute_selected(Ui *ui)
{
    CommandResult *result;
    WindowSnapshot target = {0};
    GError *error = NULL;
    if (!ui->visible || ui->selected < 0 || (guint) ui->selected >= ui->visible->len) {
        return;
    }
    result = g_ptr_array_index(ui->visible, ui->selected);
    snapshot_copy(&target, &ui->target);
    ui_hide(ui);
    if (!command_result_execute(result, &target, ui->config, &error)) {
        g_warning("windowcmd: command failed: %s", error ? error->message : "unknown error");
        g_clear_error(&error);
    }
    window_snapshot_clear(&target);
}

static void entry_changed(GtkEditable *editable, gpointer user_data)
{
    (void) editable;
    refresh_rows(user_data);
}

static void entry_activated(GtkEntry *entry, gpointer user_data)
{
    (void) entry;
    execute_selected(user_data);
}

static gboolean key_pressed(GtkEventControllerKey *controller,
                            guint keyval,
                            guint keycode,
                            GdkModifierType state,
                            gpointer user_data)
{
    Ui *ui = user_data;
    (void) controller;
    (void) keycode;
    if (keyval == GDK_KEY_Escape) {
        ui_hide(ui);
        return TRUE;
    }
    if (keyval == GDK_KEY_Tab && ui->visible && ui->selected >= 0 &&
        (guint) ui->selected < ui->visible->len) {
        CommandResult *result = g_ptr_array_index(ui->visible, ui->selected);
        if (result->command_text && *result->command_text) {
            gtk_editable_set_text(GTK_EDITABLE(ui->entry), result->command_text);
            gtk_editable_set_position(GTK_EDITABLE(ui->entry), -1);
        }
        return TRUE;
    }
    if (keyval == GDK_KEY_Up || (keyval == GDK_KEY_p && (state & GDK_CONTROL_MASK))) {
        set_selected(ui, ui_selection_move(ui->selected, ui->visible ? ui->visible->len : 0, -1));
        return TRUE;
    }
    if (keyval == GDK_KEY_Down || (keyval == GDK_KEY_n && (state & GDK_CONTROL_MASK))) {
        set_selected(ui, ui_selection_move(ui->selected, ui->visible ? ui->visible->len : 0, 1));
        return TRUE;
    }
    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
        execute_selected(ui);
        return TRUE;
    }
    return FALSE;
}

static gboolean target_timer_cb(gpointer data)
{
    Ui *ui = data;
    if (!ui->visible_now) {
        ui->target_timer = 0;
        return G_SOURCE_REMOVE;
    }
    ui_refresh_target(ui);
    return G_SOURCE_CONTINUE;
}

static void row_activated(GtkListBox *list, GtkListBoxRow *row, gpointer user_data)
{
    Ui *ui = user_data;
    (void) list;
    set_selected(ui, gtk_list_box_row_get_index(row));
    execute_selected(ui);
}

static void set_monitor(Ui *ui)
{
    GdkDisplay *display = gdk_display_get_default();
    GListModel *monitors;
    GdkMonitor *monitor;
    guint count;

    if (!display) {
        return;
    }
    monitors = gdk_display_get_monitors(display);
    count = g_list_model_get_n_items(monitors);
    if (ui->target.monitor < 0 || (guint) ui->target.monitor >= count) {
        return;
    }
    monitor = g_list_model_get_item(monitors, ui->target.monitor);
    gtk_layer_set_monitor(ui->window, monitor);
    g_object_unref(monitor);
}

static void load_css(GtkCssProvider *css)
{
    gchar *user_path = g_build_filename(g_get_user_data_dir(), "windowcmd", "windowcmd.css", NULL);
    const gchar * const *system_dirs = g_get_system_data_dirs();
    gchar *system_path = NULL;
    const gchar *path = NULL;

    if (g_file_test("data/windowcmd.css", G_FILE_TEST_EXISTS)) {
        path = "data/windowcmd.css";
    } else if (g_file_test(user_path, G_FILE_TEST_EXISTS)) {
        path = user_path;
    } else {
        for (guint i = 0; system_dirs && system_dirs[i]; i++) {
            g_free(system_path);
            system_path = g_build_filename(system_dirs[i], "windowcmd", "windowcmd.css", NULL);
            if (g_file_test(system_path, G_FILE_TEST_EXISTS)) {
                path = system_path;
                break;
            }
        }
    }
    if (path) {
        gtk_css_provider_load_from_path(css, path);
    }
    g_free(system_path);
    g_free(user_path);
}

Ui *ui_new(const Config *config, Hyprland *hyprland, HistoryStore *history, GError **error)
{
    Ui *ui;
    GtkCssProvider *css;
    GtkWidget *panel;
    GtkWidget *header;
    GtkWidget *entry_overlay;
    GtkWidget *list;
    GtkEventController *keys;
    (void) error;

    if (!gtk_layer_is_supported()) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_NOSYS, "Wayland layer-shell is not supported");
        return NULL;
    }
    ui = g_new0(Ui, 1);
    ui->config = config;
    ui->hyprland = hyprland;
    ui->history = history;
    ui->window = GTK_WINDOW(gtk_window_new());
    gtk_window_set_decorated(ui->window, FALSE);
    gtk_window_set_resizable(ui->window, FALSE);
    gtk_window_set_default_size(ui->window, config->width, 128 + config->max_results * 58);
    gtk_window_set_hide_on_close(ui->window, TRUE);

    panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_name(panel, "panel");
    header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(header), gtk_image_new_from_icon_name("application-x-executable"));
    ui->target_label = GTK_LABEL(gtk_label_new("No target"));
    gtk_label_set_xalign(ui->target_label, 0);
    gtk_label_set_ellipsize(ui->target_label, PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(GTK_WIDGET(ui->target_label), TRUE);
    gtk_widget_set_name(GTK_WIDGET(ui->target_label), "target");
    gtk_box_append(GTK_BOX(header), GTK_WIDGET(ui->target_label));
    gtk_box_append(GTK_BOX(panel), header);

    ui->entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(ui->entry, "Search commands…");
    entry_overlay = gtk_overlay_new();
    ui->ghost_label = GTK_LABEL(gtk_label_new(""));
    gtk_widget_add_css_class(GTK_WIDGET(ui->ghost_label), "ghost");
    gtk_widget_set_halign(GTK_WIDGET(ui->ghost_label), GTK_ALIGN_START);
    gtk_widget_set_margin_start(GTK_WIDGET(ui->ghost_label), 12);
    gtk_widget_set_margin_top(GTK_WIDGET(ui->ghost_label), 9);
    gtk_widget_set_can_target(GTK_WIDGET(ui->ghost_label), FALSE);
    gtk_overlay_set_child(GTK_OVERLAY(entry_overlay), GTK_WIDGET(ui->ghost_label));
    gtk_overlay_add_overlay(GTK_OVERLAY(entry_overlay), GTK_WIDGET(ui->entry));
    gtk_box_append(GTK_BOX(panel), entry_overlay);
    ui->current_description = GTK_LABEL(gtk_label_new(""));
    gtk_label_set_xalign(ui->current_description, 0);
    gtk_label_set_ellipsize(ui->current_description, PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(GTK_WIDGET(ui->current_description), "current-description");
    gtk_box_append(GTK_BOX(panel), GTK_WIDGET(ui->current_description));
    ui->list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(ui->list, GTK_SELECTION_NONE);
    gtk_widget_set_size_request(GTK_WIDGET(ui->list), -1, config->max_results * 58);
    list = GTK_WIDGET(ui->list);
    gtk_box_append(GTK_BOX(panel), list);
    ui->status_label = GTK_LABEL(gtk_label_new(""));
    gtk_label_set_xalign(ui->status_label, 0);
    gtk_box_append(GTK_BOX(panel), GTK_WIDGET(ui->status_label));
    gtk_window_set_child(ui->window, panel);

    css = gtk_css_provider_new();
    load_css(css);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                               GTK_STYLE_PROVIDER(css),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    gtk_layer_init_for_window(ui->window);
    gtk_layer_set_namespace(ui->window, "windowcmd");
    gtk_layer_set_layer(ui->window, GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_keyboard_mode(ui->window, GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
    gtk_layer_set_exclusive_zone(ui->window, 0);
    gtk_layer_set_anchor(ui->window, GTK_LAYER_SHELL_EDGE_TOP, FALSE);
    gtk_layer_set_anchor(ui->window, GTK_LAYER_SHELL_EDGE_BOTTOM, FALSE);
    gtk_layer_set_anchor(ui->window, GTK_LAYER_SHELL_EDGE_LEFT, FALSE);
    gtk_layer_set_anchor(ui->window, GTK_LAYER_SHELL_EDGE_RIGHT, FALSE);

    g_signal_connect(ui->entry, "changed", G_CALLBACK(entry_changed), ui);
    g_signal_connect(ui->entry, "activate", G_CALLBACK(entry_activated), ui);
    g_signal_connect(ui->list, "row-activated", G_CALLBACK(row_activated), ui);
    keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(key_pressed), ui);
    gtk_widget_add_controller(GTK_WIDGET(ui->window), keys);
    gtk_widget_set_visible(GTK_WIDGET(ui->window), FALSE);
    return ui;
}

void ui_show(Ui *ui, const WindowSnapshot *target)
{
    GError *error = NULL;
    gchar *header;
    if (!ui || !target) {
        return;
    }
    snapshot_copy(&ui->target, target);
    header = g_strdup_printf("%s  ·  %s", target->class_name && *target->class_name ? target->class_name : "Unknown app",
                             target->title && *target->title ? target->title : "Untitled window");
    gtk_label_set_text(ui->target_label, header);
    g_free(header);
    ui->typed_result = NULL;
    g_clear_pointer(&ui->results, g_ptr_array_unref);
    provider_collect(ui->config, &ui->target, ui->history, &ui->results, &error);
    if (error) {
        gtk_label_set_text(ui->status_label, error->message);
        g_clear_error(&error);
    }
    set_monitor(ui);
    gtk_editable_set_text(GTK_EDITABLE(ui->entry), "");
    refresh_rows(ui);
    ui->visible_now = TRUE;
    gtk_widget_set_visible(GTK_WIDGET(ui->window), TRUE);
    gtk_window_present(ui->window);
    gtk_widget_grab_focus(GTK_WIDGET(ui->entry));
    if (!ui->target_timer) {
        ui->target_timer = g_timeout_add(250, target_timer_cb, ui);
    }
}

void ui_hide(Ui *ui)
{
    if (!ui) {
        return;
    }
    ui->visible_now = FALSE;
    if (ui->target_timer) {
        g_source_remove(ui->target_timer);
        ui->target_timer = 0;
    }
    gtk_widget_set_visible(GTK_WIDGET(ui->window), FALSE);
    window_snapshot_clear(&ui->target);
}

gboolean ui_is_visible(const Ui *ui)
{
    return ui && ui->visible_now;
}

void ui_refresh_target(Ui *ui)
{
    gboolean exists = FALSE;
    GError *error = NULL;
    if (!ui || !ui->visible_now || !ui->target.address ||
        !hyprland_target_exists(ui->target.address, &exists, &error) || !exists) {
        g_clear_error(&error);
        ui_hide(ui);
    }
}

void ui_free(Ui *ui)
{
    if (!ui) {
        return;
    }
    ui_hide(ui);
    g_clear_pointer(&ui->results, g_ptr_array_unref);
    g_clear_pointer(&ui->visible, g_ptr_array_unref);
    gtk_window_destroy(ui->window);
    window_snapshot_clear(&ui->target);
    g_free(ui);
}

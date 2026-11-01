#include "settings.h"
#include "blur.h"
#include "style.h"

#include <glib/gstdio.h>
#include <math.h>
#include <unistd.h>

typedef struct {
    GtkSpinButton *font_size;
    GtkSwitch *word_wrap;
    GtkToggleButton *indent[3];
    GtkSwitch *restore_tabs;
    GtkSwitch *output;
    GtkDropDown *theme;
    GtkScale *transparency;
    GtkLabel *transparency_value;
    GtkSwitch *blur;
    gboolean blur_supported;
    GtkEntry *window_title;
    GtkScale *window_radius;
    GtkLabel *window_radius_value;
    GtkButton *background_button;
    GtkButton *background_clear;
    GtkLabel *background_path;
    GtkDropDown *background_fit;
    GtkScale *background_opacity;
    GtkLabel *background_opacity_value;
    GtkScale *background_dim;
    GtkLabel *background_dim_value;
    AppearanceSettings appearance;
    const GPtrArray *themes;
    gboolean updating;
    SettingsChangedFunc changed;
    RestoreTabsChangedFunc restore_changed;
    OutputChangedFunc output_changed;
    AppearanceChangedFunc appearance_changed;
    GtkWidget *launcher_button;
    GtkLabel *launcher_path;
    GCancellable *picker;
    GCancellable *background_picker;
    LauncherChangedFunc launcher_changed;
    gboolean stopped;
    gpointer user_data;
} Settings;

static const int indent_widths[] = {2, 4, 8};

void settings_set_launcher(GtkWidget *page, const char *path)
{
    Settings *state = g_object_get_data(G_OBJECT(page), "settings");
    g_autofree char *name = path && *path ? g_filename_display_basename(path) : g_strdup("None selected");
    gtk_label_set_text(state->launcher_path, name);
    gtk_widget_set_tooltip_text(GTK_WIDGET(state->launcher_path), path);
}

static void launcher_chosen(GObject *dialog, GAsyncResult *result, gpointer user_data)
{
    GWeakRef *owner = user_data;
    g_autoptr(GError) error = NULL;
    g_autoptr(GFile) file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(dialog), result, &error);
    g_autoptr(GtkWidget) page = g_weak_ref_get(owner);
    Settings *state = page ? g_object_get_data(G_OBJECT(page), "settings") : NULL;
    if (state && !state->stopped) {
        g_clear_object(&state->picker);
        gtk_widget_set_sensitive(state->launcher_button, TRUE);
        if (file) {
            g_autofree char *path = g_file_get_path(file);
            if (!path || !g_file_test(path, G_FILE_TEST_IS_REGULAR) || g_access(path, X_OK) != 0)
                g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Choose a local executable file.");
            else if (!state->launcher_changed || state->launcher_changed(path, &error, state->user_data))
                settings_set_launcher(page, path);
        }
        if (error && !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED) &&
            !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_CANCELLED) &&
            !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            g_autoptr(GtkAlertDialog) alert = gtk_alert_dialog_new("Could not select launcher");
            gtk_alert_dialog_set_detail(alert, error->message);
            gtk_alert_dialog_show(alert, GTK_WINDOW(gtk_widget_get_root(page)));
        }
    }
    g_weak_ref_clear(owner);
    g_free(owner);
}

static void select_launcher(GtkButton *button, gpointer user_data)
{
    GtkWidget *page = user_data;
    Settings *state = g_object_get_data(G_OBJECT(page), "settings");
    if (state->stopped || state->picker) return;
    g_autoptr(GtkFileDialog) dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select launcher");
    gtk_file_dialog_set_accept_label(dialog, "Select");
    gtk_file_dialog_set_modal(dialog, TRUE);
    state->picker = g_cancellable_new();
    gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
    GWeakRef *owner = g_new0(GWeakRef, 1);
    g_weak_ref_init(owner, page);
    gtk_file_dialog_open(dialog, GTK_WINDOW(gtk_widget_get_root(page)), state->picker, launcher_chosen, owner);
}

void settings_shutdown(GtkWidget *page)
{
    Settings *state = g_object_get_data(G_OBJECT(page), "settings");
    state->stopped = TRUE;
    if (state->picker) g_cancellable_cancel(state->picker);
    if (state->background_picker) g_cancellable_cancel(state->background_picker);
}

static void free_settings(gpointer user_data)
{
    Settings *state = user_data;
    if (state->picker) g_cancellable_cancel(state->picker);
    if (state->background_picker) g_cancellable_cancel(state->background_picker);
    g_clear_object(&state->picker);
    g_clear_object(&state->background_picker);
    appearance_settings_clear(&state->appearance);
    g_free(state);
}

void settings_set_output(GtkWidget *page, gboolean enabled)
{
    Settings *state = g_object_get_data(G_OBJECT(page), "settings");
    state->updating = TRUE;
    gtk_switch_set_active(state->output, enabled);
    state->updating = FALSE;
}

static Theme *selected_theme(Settings *state)
{
    guint index = gtk_drop_down_get_selected(state->theme);
    if (!state->themes || index == GTK_INVALID_LIST_POSITION || index >= state->themes->len)
        return NULL;
    return g_ptr_array_index(state->themes, index);
}

static void update_transparency_label(Settings *state, double transparency)
{
    g_autofree char *text = g_strdup_printf("%.0f%%", transparency * 100.0);
    gtk_label_set_text(state->transparency_value, text);
}

static void update_radius_label(Settings *state, int radius)
{
    g_autofree char *text = g_strdup_printf("%d px", radius);
    gtk_label_set_text(state->window_radius_value, text);
}

static void update_image_opacity_label(Settings *state, double opacity)
{
    g_autofree char *text = g_strdup_printf("%.0f%%", opacity * 100.0);
    gtk_label_set_text(state->background_opacity_value, text);
}

static void update_dim_label(Settings *state, double dim)
{
    g_autofree char *text = g_strdup_printf("%.0f%%", dim * 100.0);
    gtk_label_set_text(state->background_dim_value, text);
}

static void update_background_label(Settings *state, const char *path)
{
    g_autofree char *name = path && *path
        ? g_filename_display_basename(path) : g_strdup("None selected");
    gtk_label_set_text(state->background_path, name);
    gtk_widget_set_tooltip_text(GTK_WIDGET(state->background_path),
                                path && *path ? path : NULL);
}

/* The slider shows transparency (0% = opaque, 70% = most transparent),
 * while settings.ini keeps storing opacity. */
static double slider_opacity(Settings *state)
{
    double transparency = gtk_range_get_value(GTK_RANGE(state->transparency)) / 100.0;
    return CLAMP(1.0 - transparency, STYLE_OPACITY_MIN, STYLE_OPACITY_MAX);
}

static void emit_appearance(Settings *state)
{
    if (state->updating || !state->appearance_changed)
        return;
    state->appearance_changed(&state->appearance, state->user_data);
}

void settings_set_appearance(GtkWidget *page, const AppearanceSettings *settings)
{
    Settings *state = g_object_get_data(G_OBJECT(page), "settings");
    if (!state || !settings)
        return;
    state->updating = TRUE;
    appearance_settings_copy(&state->appearance, settings);
    const AppearanceSettings *current = &state->appearance;

    guint selected = GTK_INVALID_LIST_POSITION;
    if (state->themes) {
        for (guint i = 0; i < state->themes->len; ++i) {
            Theme *theme = g_ptr_array_index(state->themes, i);
            if (g_strcmp0(theme_get_id(theme), current->theme) == 0) {
                selected = i;
                break;
            }
        }
    }
    if (selected == GTK_INVALID_LIST_POSITION && state->themes && state->themes->len) {
        /* The stored theme disappeared (deleted file, renamed id...).  Show the
         * palette that is actually applied so the next save does not keep an
         * invalid id forever. */
        const char *applied = style_current_theme_id();
        for (guint i = 0; i < state->themes->len; ++i) {
            Theme *theme = g_ptr_array_index(state->themes, i);
            if (g_strcmp0(theme_get_id(theme), applied) == 0) {
                selected = i;
                break;
            }
        }
        if (selected == GTK_INVALID_LIST_POSITION)
            selected = 0;
        Theme *fallback = g_ptr_array_index(state->themes, selected);
        g_free(state->appearance.theme);
        state->appearance.theme = g_strdup(theme_get_id(fallback));
    }
    if (selected != GTK_INVALID_LIST_POSITION)
        gtk_drop_down_set_selected(state->theme, selected);

    double opacity = CLAMP(current->opacity, STYLE_OPACITY_MIN, STYLE_OPACITY_MAX);
    double transparency = 1.0 - opacity;
    gtk_range_set_value(GTK_RANGE(state->transparency), transparency * 100.0);
    update_transparency_label(state, transparency);
    gtk_switch_set_active(state->blur, current->blur);

    const char *title = current->window_title ? current->window_title : "";
    if (g_strcmp0(gtk_editable_get_text(GTK_EDITABLE(state->window_title)), title) != 0)
        gtk_editable_set_text(GTK_EDITABLE(state->window_title), title);

    gtk_range_set_value(GTK_RANGE(state->window_radius), current->window_radius);
    update_radius_label(state, current->window_radius);

    update_background_label(state, current->background_image);

    gtk_drop_down_set_selected(state->background_fit,
        g_strcmp0(current->background_fit, "contain") == 0 ? 1 : 0);
    gtk_range_set_value(GTK_RANGE(state->background_opacity),
                        current->background_opacity * 100.0);
    update_image_opacity_label(state, current->background_opacity);
    gtk_range_set_value(GTK_RANGE(state->background_dim),
                        current->background_dim * 100.0);
    update_dim_label(state, current->background_dim);

    gboolean has_image = current->background_image && *current->background_image;
    gtk_widget_set_sensitive(GTK_WIDGET(state->background_fit), has_image);
    gtk_widget_set_sensitive(GTK_WIDGET(state->background_opacity), has_image);
    gtk_widget_set_sensitive(GTK_WIDGET(state->background_opacity_value), has_image);
    gtk_widget_set_sensitive(GTK_WIDGET(state->background_dim), has_image);
    gtk_widget_set_sensitive(GTK_WIDGET(state->background_dim_value), has_image);
    gtk_widget_set_sensitive(GTK_WIDGET(state->background_clear), has_image);

    state->updating = FALSE;
}

static void theme_changed(GObject *object, GParamSpec *property, gpointer user_data)
{
    (void)object;
    (void)property;
    Settings *state = user_data;
    if (state->updating)
        return;
    Theme *theme = selected_theme(state);
    if (!theme)
        return;
    g_free(state->appearance.theme);
    state->appearance.theme = g_strdup(theme_get_id(theme));
    emit_appearance(state);
}

static void opacity_changed(GtkRange *range, gpointer user_data)
{
    Settings *state = user_data;
    double transparency = gtk_range_get_value(range) / 100.0;
    update_transparency_label(state, transparency);
    if (state->updating)
        return;
    state->appearance.opacity = slider_opacity(state);
    emit_appearance(state);
}

static void blur_changed(GObject *object, GParamSpec *property, gpointer user_data)
{
    (void)property;
    Settings *state = user_data;
    if (state->updating)
        return;
    state->appearance.blur = gtk_switch_get_active(GTK_SWITCH(object));
    emit_appearance(state);
}

static void title_changed(GtkEditable *editable, gpointer user_data)
{
    Settings *state = user_data;
    if (state->updating)
        return;
    g_free(state->appearance.window_title);
    state->appearance.window_title = g_strdup(gtk_editable_get_text(editable));
    emit_appearance(state);
}

static void radius_changed(GtkRange *range, gpointer user_data)
{
    Settings *state = user_data;
    int radius = (int)round(gtk_range_get_value(range));
    update_radius_label(state, radius);
    if (state->updating)
        return;
    state->appearance.window_radius = radius;
    emit_appearance(state);
}

static void fit_changed(GObject *object, GParamSpec *property, gpointer user_data)
{
    (void)object;
    (void)property;
    Settings *state = user_data;
    if (state->updating)
        return;
    guint selected = gtk_drop_down_get_selected(state->background_fit);
    g_free(state->appearance.background_fit);
    state->appearance.background_fit = g_strdup(selected == 1 ? "contain" : "cover");
    emit_appearance(state);
}

static void image_opacity_changed(GtkRange *range, gpointer user_data)
{
    Settings *state = user_data;
    double opacity = gtk_range_get_value(range) / 100.0;
    update_image_opacity_label(state, opacity);
    if (state->updating)
        return;
    state->appearance.background_opacity = opacity;
    emit_appearance(state);
}

static void dim_changed(GtkRange *range, gpointer user_data)
{
    Settings *state = user_data;
    double dim = gtk_range_get_value(range) / 100.0;
    update_dim_label(state, dim);
    if (state->updating)
        return;
    state->appearance.background_dim = dim;
    emit_appearance(state);
}

static void background_chosen(GObject *dialog, GAsyncResult *result, gpointer user_data)
{
    GWeakRef *owner = user_data;
    g_autoptr(GError) error = NULL;
    g_autoptr(GFile) file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(dialog), result, &error);
    g_autoptr(GtkWidget) page = g_weak_ref_get(owner);
    Settings *state = page ? g_object_get_data(G_OBJECT(page), "settings") : NULL;
    if (state && !state->stopped) {
        g_clear_object(&state->background_picker);
        gtk_widget_set_sensitive(GTK_WIDGET(state->background_button), TRUE);
        if (file) {
            g_autofree char *path = g_file_get_path(file);
            if (!path || !g_file_test(path, G_FILE_TEST_IS_REGULAR))
                g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                    "Choose a local image file.");
            else {
                g_free(state->appearance.background_image);
                state->appearance.background_image = g_strdup(path);
                update_background_label(state, path);
                emit_appearance(state);
            }
        }
        if (error && !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED) &&
            !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_CANCELLED) &&
            !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            g_autoptr(GtkAlertDialog) alert = gtk_alert_dialog_new("Could not select background image");
            gtk_alert_dialog_set_detail(alert, error->message);
            gtk_alert_dialog_show(alert, GTK_WINDOW(gtk_widget_get_root(page)));
        }
    }
    g_weak_ref_clear(owner);
    g_free(owner);
}

static void select_background(GtkButton *button, gpointer user_data)
{
    GtkWidget *page = user_data;
    Settings *state = g_object_get_data(G_OBJECT(page), "settings");
    if (state->stopped || state->background_picker)
        return;

    g_autoptr(GtkFileDialog) dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select background image");
    gtk_file_dialog_set_accept_label(dialog, "Select");
    gtk_file_dialog_set_modal(dialog, TRUE);

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Images");
    gtk_file_filter_add_mime_type(filter, "image/png");
    gtk_file_filter_add_mime_type(filter, "image/jpeg");
    gtk_file_filter_add_mime_type(filter, "image/webp");
    gtk_file_filter_add_mime_type(filter, "image/avif");
    gtk_file_filter_add_mime_type(filter, "image/svg+xml");
    gtk_file_filter_add_mime_type(filter, "image/bmp");
    gtk_file_filter_add_mime_type(filter, "image/gif");
    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, filter);
    g_object_unref(filter);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    g_object_unref(filters);

    state->background_picker = g_cancellable_new();
    gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
    GWeakRef *owner = g_new0(GWeakRef, 1);
    g_weak_ref_init(owner, page);
    gtk_file_dialog_open(dialog, GTK_WINDOW(gtk_widget_get_root(page)),
                         state->background_picker, background_chosen, owner);
}

static void clear_background(GtkButton *button, gpointer user_data)
{
    (void)button;
    Settings *state = user_data;
    if (state->stopped)
        return;
    g_free(state->appearance.background_image);
    state->appearance.background_image = NULL;
    update_background_label(state, NULL);
    emit_appearance(state);
}

static void output_changed(GObject *object, GParamSpec *property, gpointer user_data)
{
    (void)property;
    Settings *state = user_data;
    if (!state->updating && state->output_changed)
        state->output_changed(gtk_switch_get_active(GTK_SWITCH(object)), state->user_data);
}

void settings_set_restore_tabs(GtkWidget *page, gboolean enabled)
{
    Settings *state = g_object_get_data(G_OBJECT(page), "settings");
    state->updating = TRUE;
    gtk_switch_set_active(state->restore_tabs, enabled);
    state->updating = FALSE;
}

static void restore_changed(GObject *object, GParamSpec *property, gpointer user_data)
{
    (void)property;
    Settings *state = user_data;
    if (!state->updating && state->restore_changed)
        state->restore_changed(gtk_switch_get_active(GTK_SWITCH(object)), state->user_data);
}

void settings_set_editor(GtkWidget *page, const EditorSettings *settings)
{
    Settings *state = g_object_get_data(G_OBJECT(page), "settings");
    state->updating = TRUE;
    gtk_spin_button_set_value(state->font_size, settings->font_size);
    gtk_switch_set_active(state->word_wrap, settings->word_wrap);
    for (guint i = 0; i < G_N_ELEMENTS(indent_widths); ++i)
        if (settings->indent_width == indent_widths[i])
            gtk_toggle_button_set_active(state->indent[i], TRUE);
    state->updating = FALSE;
}

static void values_changed(GtkWidget *widget, gpointer user_data)
{
    Settings *state = user_data;
    if (state->updating || !state->changed ||
        (GTK_IS_TOGGLE_BUTTON(widget) && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))))
        return;
    EditorSettings settings = {
        .font_size = gtk_spin_button_get_value_as_int(state->font_size),
        .word_wrap = gtk_switch_get_active(state->word_wrap),
        .indent_width = 4
    };
    for (guint i = 0; i < G_N_ELEMENTS(indent_widths); ++i)
        if (gtk_toggle_button_get_active(state->indent[i]))
            settings.indent_width = indent_widths[i];
    state->changed(&settings, state->user_data);
}

static void wrap_changed(GObject *object, GParamSpec *property, gpointer user_data)
{
    (void)property;
    values_changed(GTK_WIDGET(object), user_data);
}

static GtkWidget *label_new(const char *text, const char *style)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    if (style)
        gtk_widget_add_css_class(label, style);
    return label;
}

static void add_row(GtkWidget *group, const char *title, const char *description, GtkWidget *control)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_widget_add_css_class(row, "settings-row");
    GtkWidget *labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand(labels, TRUE);
    gtk_widget_set_valign(labels, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(labels), label_new(title, "settings-label"));
    GtkWidget *detail = label_new(description, "settings-description");
    gtk_label_set_wrap(GTK_LABEL(detail), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(detail), 33);
    gtk_box_append(GTK_BOX(labels), detail);
    gtk_box_append(GTK_BOX(row), labels);
    GtkWidget *slot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(slot, 150, -1);
    gtk_widget_set_valign(slot, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(control, TRUE);
    gtk_widget_set_halign(control, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(slot), control);
    gtk_box_append(GTK_BOX(row), slot);
    gtk_box_append(GTK_BOX(group), row);
}

static GtkWidget *section_new(GtkWidget *column, const char *title, GtkWidget **out_group)
{
    GtkWidget *section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_hexpand(section, TRUE);
    gtk_box_append(GTK_BOX(column), section);
    gtk_box_append(GTK_BOX(section), label_new(title, "settings-group-title"));
    GtkWidget *group = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(group, "settings-group");
    gtk_widget_set_hexpand(group, TRUE);
    gtk_box_append(GTK_BOX(section), group);
    *out_group = group;
    return section;
}

GtkWidget *settings_new(const EditorSettings *settings, const GPtrArray *themes,
                       SettingsChangedFunc changed,
                       RestoreTabsChangedFunc restore_callback, OutputChangedFunc output_callback,
                       LauncherChangedFunc launcher_callback,
                       AppearanceChangedFunc appearance_callback, gpointer user_data)
{
    Settings *state = g_new0(Settings, 1);
    state->themes = themes;
    state->changed = changed;
    state->restore_changed = restore_callback;
    state->output_changed = output_callback;
    state->launcher_changed = launcher_callback;
    state->appearance_changed = appearance_callback;
    state->user_data = user_data;
    GtkWidget *page = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(page), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    g_object_set_data_full(G_OBJECT(page), "settings", state, free_settings);
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(content, "settings-page");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(page), content);
    gtk_box_append(GTK_BOX(content), label_new("Settings", "settings-title"));

    GtkWidget *columns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    gtk_widget_add_css_class(columns, "settings-columns");
    gtk_widget_set_hexpand(columns, TRUE);
    gtk_box_append(GTK_BOX(content), columns);

    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_hexpand(left, TRUE);
    gtk_widget_set_valign(left, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(columns), left);

    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_hexpand(right, TRUE);
    gtk_widget_set_valign(right, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(columns), right);

    GtkWidget *editor_group = NULL;
    section_new(left, "Editor", &editor_group);

    GtkWidget *font = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    state->font_size = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(
        EDITOR_FONT_SIZE_MIN, EDITOR_FONT_SIZE_MAX, 1));
    gtk_widget_add_css_class(GTK_WIDGET(state->font_size), "font-size");
    gtk_spin_button_set_numeric(state->font_size, TRUE);
    gtk_spin_button_set_update_policy(state->font_size, GTK_UPDATE_IF_VALID);
    gtk_editable_set_width_chars(GTK_EDITABLE(state->font_size), 2);
    gtk_editable_set_alignment(GTK_EDITABLE(state->font_size), 0.5);
    gtk_accessible_update_property(GTK_ACCESSIBLE(state->font_size),
        GTK_ACCESSIBLE_PROPERTY_LABEL, "Editor font size in pixels", -1);
    gtk_box_append(GTK_BOX(font), GTK_WIDGET(state->font_size));
    gtk_box_append(GTK_BOX(font), label_new("px", "settings-description"));
    add_row(editor_group, "Font size", "Editor text and line numbers", font);

    state->word_wrap = GTK_SWITCH(gtk_switch_new());
    gtk_widget_add_css_class(GTK_WIDGET(state->word_wrap), "word-wrap");
    gtk_accessible_update_property(GTK_ACCESSIBLE(state->word_wrap),
        GTK_ACCESSIBLE_PROPERTY_LABEL, "Word wrap", -1);
    add_row(editor_group, "Word wrap", "Wrap long lines to fit the editor", GTK_WIDGET(state->word_wrap));

    GtkWidget *indent = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(indent, "indent-options");
    for (guint i = 0; i < G_N_ELEMENTS(indent_widths); ++i) {
        g_autofree char *text = g_strdup_printf("%d", indent_widths[i]);
        state->indent[i] = GTK_TOGGLE_BUTTON(gtk_toggle_button_new_with_label(text));
        if (i)
            gtk_toggle_button_set_group(state->indent[i], state->indent[0]);
        g_autofree char *label = g_strdup_printf("Indent with %d spaces", indent_widths[i]);
        gtk_accessible_update_property(GTK_ACCESSIBLE(state->indent[i]),
            GTK_ACCESSIBLE_PROPERTY_LABEL, label, -1);
        gtk_box_append(GTK_BOX(indent), GTK_WIDGET(state->indent[i]));
        g_signal_connect(state->indent[i], "toggled", G_CALLBACK(values_changed), state);
    }
    add_row(editor_group, "Indentation", "Spaces used by Tab and auto-indent", indent);
    settings_set_editor(page, settings);
    g_signal_connect(state->font_size, "value-changed", G_CALLBACK(values_changed), state);
    g_signal_connect(state->word_wrap, "notify::active", G_CALLBACK(wrap_changed), state);

    GtkWidget *session_group = NULL;
    section_new(left, "Session", &session_group);
    state->restore_tabs = GTK_SWITCH(gtk_switch_new());
    gtk_widget_add_css_class(GTK_WIDGET(state->restore_tabs), "restore-tabs");
    gtk_accessible_update_property(GTK_ACCESSIBLE(state->restore_tabs),
        GTK_ACCESSIBLE_PROPERTY_LABEL, "Restore tabs on startup", -1);
    gtk_switch_set_active(state->restore_tabs, TRUE);
    add_row(session_group, "Restore tabs on startup", "Reopen tabs and unsaved drafts", GTK_WIDGET(state->restore_tabs));
    g_signal_connect(state->restore_tabs, "notify::active", G_CALLBACK(restore_changed), state);

    GtkWidget *output_group = NULL;
    section_new(left, "Output", &output_group);
    state->output = GTK_SWITCH(gtk_switch_new());
    gtk_widget_add_css_class(GTK_WIDGET(state->output), "redirect-output");
    gtk_accessible_update_property(GTK_ACCESSIBLE(state->output),
        GTK_ACCESSIBLE_PROPERTY_LABEL, "Redirect script output", -1);
    add_row(output_group, "Redirect script output", "Show your scripts’ output below the editor", GTK_WIDGET(state->output));
    g_signal_connect(state->output, "notify::active", G_CALLBACK(output_changed), state);

    GtkWidget *appearance_group = NULL;
    section_new(right, "Appearance", &appearance_group);
    GtkStringList *theme_names = gtk_string_list_new(NULL);
    if (themes) {
        for (guint i = 0; i < themes->len; ++i) {
            Theme *theme = g_ptr_array_index(themes, i);
            gtk_string_list_append(theme_names, theme_get_name(theme));
        }
    }
    state->theme = GTK_DROP_DOWN(gtk_drop_down_new(G_LIST_MODEL(theme_names), NULL));
    /* gtk_drop_down_new takes ownership of the model reference. */
    gtk_widget_add_css_class(GTK_WIDGET(state->theme), "theme-picker");
    gtk_accessible_update_property(GTK_ACCESSIBLE(state->theme),
        GTK_ACCESSIBLE_PROPERTY_LABEL, "Color scheme", -1);
    add_row(appearance_group, "Color scheme", "Bundled or user themes from themes/*.json",
            GTK_WIDGET(state->theme));
    g_signal_connect(state->theme, "notify::selected", G_CALLBACK(theme_changed), state);

    GtkWidget *opacity_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    state->transparency = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 70, 1));
    gtk_scale_set_draw_value(state->transparency, FALSE);
    gtk_widget_set_hexpand(GTK_WIDGET(state->transparency), TRUE);
    gtk_widget_set_size_request(GTK_WIDGET(state->transparency), 100, -1);
    gtk_range_set_value(GTK_RANGE(state->transparency), 4);
    gtk_accessible_update_property(GTK_ACCESSIBLE(state->transparency),
        GTK_ACCESSIBLE_PROPERTY_LABEL, "Window transparency percentage", -1);
    state->transparency_value = GTK_LABEL(gtk_label_new("4%"));
    gtk_widget_add_css_class(GTK_WIDGET(state->transparency_value), "settings-description");
    gtk_widget_set_size_request(GTK_WIDGET(state->transparency_value), 38, -1);
    gtk_label_set_xalign(state->transparency_value, 1);
    gtk_box_append(GTK_BOX(opacity_box), GTK_WIDGET(state->transparency));
    gtk_box_append(GTK_BOX(opacity_box), GTK_WIDGET(state->transparency_value));
    add_row(appearance_group, "Window transparency", "Higher values let more of the desktop show through",
            opacity_box);
    g_signal_connect(state->transparency, "value-changed", G_CALLBACK(opacity_changed), state);

    state->blur_supported = blur_supported();
    state->blur = GTK_SWITCH(gtk_switch_new());
    gtk_widget_add_css_class(GTK_WIDGET(state->blur), "blur-background");
    gtk_accessible_update_property(GTK_ACCESSIBLE(state->blur),
        GTK_ACCESSIBLE_PROPERTY_LABEL, "Blur background", -1);
    gtk_switch_set_active(state->blur, state->blur_supported);
    gtk_widget_set_sensitive(GTK_WIDGET(state->blur), state->blur_supported);
    add_row(appearance_group, "Blur background",
            state->blur_supported
                ? "The compositor blurs the desktop behind translucent areas"
                : "This compositor does not support background blur",
            GTK_WIDGET(state->blur));
    g_signal_connect(state->blur, "notify::active", G_CALLBACK(blur_changed), state);

    GtkWidget *window_group = NULL;
    section_new(right, "Window", &window_group);

    state->window_title = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(state->window_title, APPEARANCE_TITLE_DEFAULT);
    gtk_editable_set_max_width_chars(GTK_EDITABLE(state->window_title), 24);
    gtk_widget_set_size_request(GTK_WIDGET(state->window_title), 150, -1);
    gtk_accessible_update_property(GTK_ACCESSIBLE(state->window_title),
        GTK_ACCESSIBLE_PROPERTY_LABEL, "Window title", -1);
    add_row(window_group, "Window title", "Static text shown in the title bar",
            GTK_WIDGET(state->window_title));
    g_signal_connect(state->window_title, "changed", G_CALLBACK(title_changed), state);

    GtkWidget *radius_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    state->window_radius = GTK_SCALE(gtk_scale_new_with_range(
        GTK_ORIENTATION_HORIZONTAL, 0, APPEARANCE_RADIUS_MAX, 1));
    gtk_scale_set_draw_value(state->window_radius, FALSE);
    gtk_widget_set_hexpand(GTK_WIDGET(state->window_radius), TRUE);
    gtk_widget_set_size_request(GTK_WIDGET(state->window_radius), 100, -1);
    gtk_range_set_value(GTK_RANGE(state->window_radius), APPEARANCE_RADIUS_DEFAULT);
    gtk_accessible_update_property(GTK_ACCESSIBLE(state->window_radius),
        GTK_ACCESSIBLE_PROPERTY_LABEL, "Window corner radius in pixels", -1);
    state->window_radius_value = GTK_LABEL(gtk_label_new("12 px"));
    gtk_widget_add_css_class(GTK_WIDGET(state->window_radius_value), "settings-description");
    gtk_widget_set_size_request(GTK_WIDGET(state->window_radius_value), 42, -1);
    gtk_label_set_xalign(state->window_radius_value, 1);
    gtk_box_append(GTK_BOX(radius_box), GTK_WIDGET(state->window_radius));
    gtk_box_append(GTK_BOX(radius_box), GTK_WIDGET(state->window_radius_value));
    add_row(window_group, "Corner radius", "Round the window corners (0–24 px)", radius_box);
    g_signal_connect(state->window_radius, "value-changed", G_CALLBACK(radius_changed), state);

    GtkWidget *background = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *background_buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    state->background_button = GTK_BUTTON(gtk_button_new_with_label("Select…"));
    gtk_accessible_update_property(GTK_ACCESSIBLE(state->background_button),
        GTK_ACCESSIBLE_PROPERTY_LABEL, "Select background image", -1);
    state->background_clear = GTK_BUTTON(gtk_button_new_with_label("×"));
    gtk_widget_set_size_request(GTK_WIDGET(state->background_clear), 34, -1);
    gtk_widget_set_tooltip_text(GTK_WIDGET(state->background_clear), "Remove background image");
    gtk_accessible_update_property(GTK_ACCESSIBLE(state->background_clear),
        GTK_ACCESSIBLE_PROPERTY_LABEL, "Remove background image", -1);
    gtk_box_append(GTK_BOX(background_buttons), GTK_WIDGET(state->background_button));
    gtk_box_append(GTK_BOX(background_buttons), GTK_WIDGET(state->background_clear));
    gtk_box_append(GTK_BOX(background), background_buttons);
    state->background_path = GTK_LABEL(gtk_label_new("None selected"));
    gtk_label_set_ellipsize(state->background_path, PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(state->background_path, 18);
    gtk_label_set_xalign(state->background_path, 1);
    gtk_widget_add_css_class(GTK_WIDGET(state->background_path), "settings-description");
    gtk_box_append(GTK_BOX(background), GTK_WIDGET(state->background_path));
    add_row(window_group, "Background image", "Painted behind the theme tint", background);
    g_signal_connect(state->background_button, "clicked", G_CALLBACK(select_background), page);
    g_signal_connect(state->background_clear, "clicked", G_CALLBACK(clear_background), state);

    state->background_fit = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(
        (const char *const[]){"Cover", "Contain", NULL}));
    gtk_accessible_update_property(GTK_ACCESSIBLE(state->background_fit),
        GTK_ACCESSIBLE_PROPERTY_LABEL, "Background image fit", -1);
    add_row(window_group, "Image fit", "Cover fills the window; contain shows the whole image",
            GTK_WIDGET(state->background_fit));
    g_signal_connect(state->background_fit, "notify::selected", G_CALLBACK(fit_changed), state);

    GtkWidget *image_opacity_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    state->background_opacity = GTK_SCALE(gtk_scale_new_with_range(
        GTK_ORIENTATION_HORIZONTAL, 0, 100, 1));
    gtk_scale_set_draw_value(state->background_opacity, FALSE);
    gtk_widget_set_hexpand(GTK_WIDGET(state->background_opacity), TRUE);
    gtk_widget_set_size_request(GTK_WIDGET(state->background_opacity), 100, -1);
    gtk_range_set_value(GTK_RANGE(state->background_opacity), 100);
    gtk_accessible_update_property(GTK_ACCESSIBLE(state->background_opacity),
        GTK_ACCESSIBLE_PROPERTY_LABEL, "Background image opacity percentage", -1);
    state->background_opacity_value = GTK_LABEL(gtk_label_new("100%"));
    gtk_widget_add_css_class(GTK_WIDGET(state->background_opacity_value), "settings-description");
    gtk_widget_set_size_request(GTK_WIDGET(state->background_opacity_value), 38, -1);
    gtk_label_set_xalign(state->background_opacity_value, 1);
    gtk_box_append(GTK_BOX(image_opacity_box), GTK_WIDGET(state->background_opacity));
    gtk_box_append(GTK_BOX(image_opacity_box), GTK_WIDGET(state->background_opacity_value));
    add_row(window_group, "Image opacity", "Let the window color show through the image",
            image_opacity_box);
    g_signal_connect(state->background_opacity, "value-changed",
                     G_CALLBACK(image_opacity_changed), state);

    GtkWidget *dim_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    state->background_dim = GTK_SCALE(gtk_scale_new_with_range(
        GTK_ORIENTATION_HORIZONTAL, 0, (int)(APPEARANCE_BACKGROUND_DIM_MAX * 100.0), 1));
    gtk_scale_set_draw_value(state->background_dim, FALSE);
    gtk_widget_set_hexpand(GTK_WIDGET(state->background_dim), TRUE);
    gtk_widget_set_size_request(GTK_WIDGET(state->background_dim), 100, -1);
    gtk_range_set_value(GTK_RANGE(state->background_dim),
                        (int)(APPEARANCE_BACKGROUND_DIM_DEFAULT * 100.0));
    gtk_accessible_update_property(GTK_ACCESSIBLE(state->background_dim),
        GTK_ACCESSIBLE_PROPERTY_LABEL, "Background dimming percentage", -1);
    state->background_dim_value = GTK_LABEL(gtk_label_new("35%"));
    gtk_widget_add_css_class(GTK_WIDGET(state->background_dim_value), "settings-description");
    gtk_widget_set_size_request(GTK_WIDGET(state->background_dim_value), 38, -1);
    gtk_label_set_xalign(state->background_dim_value, 1);
    gtk_box_append(GTK_BOX(dim_box), GTK_WIDGET(state->background_dim));
    gtk_box_append(GTK_BOX(dim_box), GTK_WIDGET(state->background_dim_value));
    add_row(window_group, "Dim background", "Darken the image so text stays readable", dim_box);
    g_signal_connect(state->background_dim, "value-changed", G_CALLBACK(dim_changed), state);

    gtk_widget_set_sensitive(GTK_WIDGET(state->background_fit), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(state->background_opacity), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(state->background_opacity_value), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(state->background_dim), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(state->background_dim_value), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(state->background_clear), FALSE);

    GtkWidget *launcher_group = NULL;
    section_new(right, "Profiles", &launcher_group);
    GtkWidget *launcher = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    state->launcher_button = gtk_button_new_with_label("Select…");
    gtk_accessible_update_property(GTK_ACCESSIBLE(state->launcher_button),
        GTK_ACCESSIBLE_PROPERTY_LABEL, "Select launcher", -1);
    gtk_box_append(GTK_BOX(launcher), state->launcher_button);
    state->launcher_path = GTK_LABEL(gtk_label_new("None selected"));
    gtk_label_set_ellipsize(state->launcher_path, PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(state->launcher_path, 16);
    gtk_widget_add_css_class(GTK_WIDGET(state->launcher_path), "settings-description");
    gtk_box_append(GTK_BOX(launcher), GTK_WIDGET(state->launcher_path));
    add_row(launcher_group, "Launcher", "Executable used to launch Profiles", launcher);
    g_signal_connect(state->launcher_button, "clicked", G_CALLBACK(select_launcher), page);

    return page;
}

#include "style.h"

#include <glib/gstdio.h>
#include <math.h>

typedef struct {
    GtkCssProvider *base;
    GtkCssProvider *theme;
    GtkCssProvider *appearance;
    GPtrArray *themes;
    Theme *current;
    double opacity;
    char *scheme_directory;
    char *scheme_id;
    char *warned_missing_image;
} StyleState;

static StyleState *style;

static void parsing_error(GtkCssProvider *provider, GtkCssSection *section,
                          const GError *error, gpointer user_data)
{
    (void)provider;
    (void)section;
    (void)user_data;
    g_error("Invalid stylesheet: %s", error->message);
}

static Theme *find_theme(const char *theme_id)
{
    if (!style || !style->themes || !theme_id)
        return NULL;
    for (guint i = 0; i < style->themes->len; ++i) {
        Theme *theme = g_ptr_array_index(style->themes, i);
        if (g_strcmp0(theme_get_id(theme), theme_id) == 0)
            return theme;
    }
    return NULL;
}

static void register_scheme_directory(const char *directory)
{
    GtkSourceStyleSchemeManager *manager = gtk_source_style_scheme_manager_get_default();
    const char *const *existing = gtk_source_style_scheme_manager_get_search_path(manager);
    GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);
    gboolean found = FALSE;
    if (existing) {
        for (const char *const *path = existing; *path; ++path) {
            if (g_strcmp0(*path, directory) == 0)
                found = TRUE;
            g_ptr_array_add(paths, g_strdup(*path));
        }
    }
    if (!found) {
        g_ptr_array_add(paths, g_strdup(directory));
        g_ptr_array_add(paths, NULL);
        gtk_source_style_scheme_manager_set_search_path(manager,
            (const char *const *)paths->pdata);
    }
    g_ptr_array_unref(paths);
}

static void write_editor_scheme(const Theme *theme)
{
    g_autofree char *id = theme_editor_scheme_id(theme);
    g_autofree char *xml = theme_build_editor_scheme_xml(theme);
    g_autofree char *filename = g_strconcat(id, ".xml", NULL);
    g_autofree char *path = g_build_filename(style->scheme_directory, filename, NULL);

    g_autoptr(GError) error = NULL;
    if (!g_file_set_contents(path, xml, -1, &error)) {
        g_warning("Could not write editor scheme %s: %s", path, error->message);
        return;
    }

    register_scheme_directory(style->scheme_directory);
    GtkSourceStyleSchemeManager *manager = gtk_source_style_scheme_manager_get_default();
    gtk_source_style_scheme_manager_force_rescan(manager);

    if (!gtk_source_style_scheme_manager_get_scheme(manager, id))
        g_warning("Generated editor scheme %s was not loaded", id);
    g_free(style->scheme_id);
    style->scheme_id = g_steal_pointer(&id);
}

const GPtrArray *style_theme_list(void)
{
    return style ? style->themes : NULL;
}

const char *style_default_theme_id(void)
{
    return "aurora";
}

const Theme *style_current_theme(void)
{
    return style ? style->current : NULL;
}

const char *style_current_theme_id(void)
{
    return style && style->current ? theme_get_id(style->current) : NULL;
}

double style_current_opacity(void)
{
    return style ? style->opacity : 0.0;
}

gboolean style_set_theme(const char *theme_id, double opacity)
{
    if (!style)
        return FALSE;
    Theme *theme = find_theme(theme_id);
    if (!theme)
        return FALSE;

    if (opacity <= 0.0)
        opacity = theme_get_opacity(theme);
    opacity = CLAMP(opacity, STYLE_OPACITY_MIN, STYLE_OPACITY_MAX);

    g_autofree char *css = theme_build_css(theme, opacity);
    gtk_css_provider_load_from_string(style->theme, css);
    gboolean palette_changed = style->current != theme;
    style->current = theme;
    style->opacity = opacity;
    /* The editor scheme only depends on the palette, not on opacity. */
    if (palette_changed)
        write_editor_scheme(theme);
    return TRUE;
}

static void append_rgba(GString *css, const GdkRGBA *color, double alpha)
{
    char alpha_text[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_formatd(alpha_text, sizeof alpha_text, "%.3f",
                    CLAMP(color->alpha * alpha, 0.0, 1.0));
    g_string_append_printf(css,
        "rgba(%u, %u, %u, %s)",
        (unsigned)CLAMP(round(color->red * 255.0), 0, 255),
        (unsigned)CLAMP(round(color->green * 255.0), 0, 255),
        (unsigned)CLAMP(round(color->blue * 255.0), 0, 255),
        alpha_text);
}

/* A softened copy of the theme's window gradient.  Painting it over the
 * background image keeps the palette recognisable without hiding the image. */
static void append_tint(GString *css, const Theme *theme, double alpha)
{
    const ThemePalette *palette = theme_get_palette(theme);
    const ThemeGradient *gradient = &theme->window_gradient;

    g_string_append(css, "linear-gradient(");
    if (gradient->defined && gradient->n_stops >= 2) {
        g_string_append_printf(css, "%.0fdeg, ", gradient->angle);
        for (guint i = 0; i < gradient->n_stops; ++i) {
            if (i)
                g_string_append(css, ", ");
            append_rgba(css, &gradient->stops[i], alpha);
            g_string_append_printf(css, " %.0f%%",
                                   100.0 * i / (gradient->n_stops - 1));
        }
    } else {
        g_string_append(css, "180deg, ");
        append_rgba(css, &palette->background, alpha);
        g_string_append(css, " 0%, ");
        append_rgba(css, &palette->background_alt, alpha);
        g_string_append(css, " 55%, ");
        append_rgba(css, &palette->surface, alpha);
        g_string_append(css, " 100%");
    }
    g_string_append(css, ")");
}

static char *appearance_build_css(const AppearanceSettings *settings)
{
    GString *css = g_string_new("/* Generated from the appearance settings; do not edit. */\n");
    int radius = settings ? settings->window_radius : APPEARANCE_RADIUS_DEFAULT;
    radius = CLAMP(radius, 0, APPEARANCE_RADIUS_MAX);
    g_string_append_printf(css,
        ".relapse-ui, .relapse-ui * { --window-radius: %dpx; }\n", radius);

    if (!style || !style->current || !settings)
        return g_string_free(css, FALSE);

    const char *image = settings->background_image;
    if (!image || !*image)
        return g_string_free(css, FALSE);
    if (!g_file_test(image, G_FILE_TEST_IS_REGULAR)) {
        if (g_strcmp0(style->warned_missing_image, image) != 0) {
            g_warning("Background image %s is missing; keeping the theme background", image);
            g_free(style->warned_missing_image);
            style->warned_missing_image = g_strdup(image);
        }
        return g_string_free(css, FALSE);
    }
    g_clear_pointer(&style->warned_missing_image, g_free);

    g_autofree char *uri = g_filename_to_uri(image, NULL, NULL);
    if (!uri)
        return g_string_free(css, FALSE);

    double image_opacity = CLAMP(settings->background_opacity, 0.0, 1.0);
    double dim = CLAMP(settings->background_dim, 0.0, APPEARANCE_BACKGROUND_DIM_MAX);
    gboolean contain = g_strcmp0(settings->background_fit, "contain") == 0;

    /* Layer order (first is topmost): dim overlay, theme tint, image. */
    g_string_append(css, "window.relapse-ui {\n    background-image: ");
    char dim_text[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_formatd(dim_text, sizeof dim_text, "%.3f", dim);
    g_string_append_printf(css,
        "linear-gradient(rgba(0, 0, 0, %s), rgba(0, 0, 0, %s)),\n        ",
        dim_text, dim_text);
    append_tint(css, style->current, 0.35);
    g_string_append(css, ",\n        ");
    if (image_opacity >= 0.999) {
        g_string_append_printf(css, "url(\"%s\")", uri);
    } else {
        g_string_append_printf(css,
            "cross-fade(url(\"%s\"), linear-gradient(transparent, transparent), %.0f%%)",
            uri, image_opacity * 100.0);
    }
    g_string_append(css, ",\n        ");
    /* Keep the theme gradient underneath so "contain" letterboxing matches
     * the normal window background. */
    append_tint(css, style->current, style->opacity);
    g_string_append_printf(css,
        ";\n"
        "    background-size: 100%% 100%%, 100%% 100%%, %s, 100%% 100%%;\n"
        "    background-position: center, center, center, center;\n"
        "    background-repeat: no-repeat, no-repeat, no-repeat, no-repeat;\n"
        "}\n",
        contain ? "contain" : "cover");
    return g_string_free(css, FALSE);
}

void style_set_appearance(const AppearanceSettings *settings)
{
    if (!style || !style->appearance)
        return;
    g_autofree char *css = appearance_build_css(settings);
    gtk_css_provider_load_from_string(style->appearance, css);
}

GtkSourceStyleScheme *style_editor_scheme(void)
{
    if (!style || !style->scheme_id)
        return NULL;
    return gtk_source_style_scheme_manager_get_scheme(
        gtk_source_style_scheme_manager_get_default(), style->scheme_id);
}

static void apply_text_tag(GtkTextBuffer *buffer, const char *name, const GdkRGBA *color)
{
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
    GtkTextTag *tag = gtk_text_tag_table_lookup(table, name);
    if (!tag)
        tag = gtk_text_buffer_create_tag(buffer, name, NULL);
    g_autofree char *hex = theme_color_to_hex(color);
    g_object_set(tag, "foreground", hex, NULL);
}

void style_apply_text_tags(GtkTextBuffer *buffer)
{
    if (!style || !style->current || !GTK_IS_TEXT_BUFFER(buffer))
        return;
    const ThemePalette *palette = theme_get_palette(style->current);
    apply_text_tag(buffer, "print", &palette->foreground);
    apply_text_tag(buffer, "info", &palette->info);
    apply_text_tag(buffer, "warn", &palette->warning);
    apply_text_tag(buffer, "error", &palette->danger);
}

void style_install(GdkDisplay *display)
{
    if (style)
        return;
    style = g_new0(StyleState, 1);
    style->themes = theme_list_load();

    style->base = gtk_css_provider_new();
    g_signal_connect(style->base, "parsing-error", G_CALLBACK(parsing_error), NULL);
    gtk_css_provider_load_from_resource(style->base,
                                        "/org/relapse/RelapseGtk/style.css");
    gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(style->base),
                                               GTK_STYLE_PROVIDER_PRIORITY_USER + 1);

    style->theme = gtk_css_provider_new();
    g_signal_connect(style->theme, "parsing-error", G_CALLBACK(parsing_error), NULL);
    gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(style->theme),
                                               GTK_STYLE_PROVIDER_PRIORITY_USER + 2);

    style->appearance = gtk_css_provider_new();
    g_signal_connect(style->appearance, "parsing-error", G_CALLBACK(parsing_error), NULL);
    gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(style->appearance),
                                               GTK_STYLE_PROVIDER_PRIORITY_USER + 3);

    gtk_source_init();
    style->scheme_directory = g_build_filename(g_get_user_cache_dir(), "relapsegtk",
                                               "schemes", NULL);
    if (g_mkdir_with_parents(style->scheme_directory, 0700) != 0)
        g_warning("Could not create editor scheme directory %s", style->scheme_directory);

    if (style->themes->len) {
        Theme *default_theme = find_theme(style_default_theme_id());
        if (!default_theme)
            default_theme = g_ptr_array_index(style->themes, 0);
        style_set_theme(theme_get_id(default_theme), 0.0);
    } else {
        g_warning("No color schemes were found");
    }
}

void style_set_editor_font_size(GdkDisplay *display, int size)
{
    GtkCssProvider *provider = g_object_get_data(G_OBJECT(display), "editor-font-provider");
    if (!provider) {
        provider = gtk_css_provider_new();
        g_signal_connect(provider, "parsing-error", G_CALLBACK(parsing_error), NULL);
        g_object_set_data_full(G_OBJECT(display), "editor-font-provider", provider, g_object_unref);
        gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(provider),
                                                   GTK_STYLE_PROVIDER_PRIORITY_USER + 4);
    }
    g_autofree char *css = g_strdup_printf(
        ".relapse-ui textview.code-editor, .relapse-ui textview.code-editor * { font-size: %dpx; }", size);
    gtk_css_provider_load_from_string(provider, css);
}

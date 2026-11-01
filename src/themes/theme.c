#include "theme.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <math.h>
#include <string.h>

#define RESOURCE_THEME_DIR "/org/relapse/RelapseGtk/themes"
#define DEFAULT_OPACITY 0.96
#define OPACITY_MIN 0.30
#define OPACITY_MAX 1.00

static GdkRGBA mix_color(const GdkRGBA *a, const GdkRGBA *b, double amount)
{
    amount = CLAMP(amount, 0.0, 1.0);
    return (GdkRGBA){
        .red = a->red + (b->red - a->red) * amount,
        .green = a->green + (b->green - a->green) * amount,
        .blue = a->blue + (b->blue - a->blue) * amount,
        .alpha = a->alpha + (b->alpha - a->alpha) * amount,
    };
}

char *theme_color_to_hex(const GdkRGBA *color)
{
    g_return_val_if_fail(color != NULL, NULL);
    return g_strdup_printf("#%02x%02x%02x",
        (unsigned)CLAMP(round(color->red * 255.0), 0, 255),
        (unsigned)CLAMP(round(color->green * 255.0), 0, 255),
        (unsigned)CLAMP(round(color->blue * 255.0), 0, 255));
}

const char *theme_get_id(const Theme *theme)
{
    return theme ? theme->id : NULL;
}

const char *theme_get_name(const Theme *theme)
{
    return theme ? theme->name : NULL;
}

const char *theme_get_description(const Theme *theme)
{
    return theme ? theme->description : NULL;
}

double theme_get_opacity(const Theme *theme)
{
    return theme ? theme->opacity : DEFAULT_OPACITY;
}

const ThemePalette *theme_get_palette(const Theme *theme)
{
    return theme ? &theme->colors : NULL;
}

void theme_free(Theme *theme)
{
    if (!theme)
        return;
    g_free(theme->id);
    g_free(theme->name);
    g_free(theme->description);
    g_free(theme->window_gradient.stops);
    g_free(theme->accent_gradient.stops);
    g_free(theme);
}

char *theme_editor_scheme_id(const Theme *theme)
{
    g_return_val_if_fail(theme != NULL, NULL);
    GString *safe = g_string_new("relapse-");
    for (const char *p = theme->id; p && *p; ++p) {
        if (g_ascii_isalnum(*p) || *p == '-' || *p == '_')
            g_string_append_c(safe, *p);
        else
            g_string_append_c(safe, '-');
    }
    return g_string_free(safe, FALSE);
}

static gboolean color_from_object(JsonObject *object, const char *key,
                                  GdkRGBA *out, GError **error)
{
    if (!object || !json_object_has_member(object, key)) {
        g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                    "theme is missing colors.%s", key);
        return FALSE;
    }
    const char *value = json_object_get_string_member(object, key);
    if (!value || !gdk_rgba_parse(out, value)) {
        g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                    "colors.%s is not a valid CSS colour", key);
        return FALSE;
    }
    return TRUE;
}

static gboolean optional_color(JsonObject *object, const char *key,
                               GdkRGBA *out, const GdkRGBA *fallback, GError **error)
{
    if (!object || !json_object_has_member(object, key)) {
        *out = *fallback;
        return TRUE;
    }
    return color_from_object(object, key, out, error);
}

static gboolean gradient_from_object(JsonObject *object, const char *key,
                                     ThemeGradient *out, GError **error)
{
    out->defined = FALSE;
    if (!object || !json_object_has_member(object, key))
        return TRUE;

    JsonNode *node = json_object_get_member(object, key);
    if (!node || !JSON_NODE_HOLDS_OBJECT(node)) {
        g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                    "%s must be an object with a stops array", key);
        return FALSE;
    }
    JsonObject *gradient = json_node_get_object(node);
    JsonNode *stops_node = json_object_get_member(gradient, "stops");
    if (!stops_node || !JSON_NODE_HOLDS_ARRAY(stops_node)) {
        g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                    "%s.stops must be an array of CSS colours", key);
        return FALSE;
    }

    JsonArray *stops = json_node_get_array(stops_node);
    guint length = json_array_get_length(stops);
    if (length < 2) {
        g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                    "%s.stops needs at least two colours", key);
        return FALSE;
    }

    out->stops = g_new0(GdkRGBA, length);
    out->n_stops = length;
    for (guint i = 0; i < length; ++i) {
        JsonNode *element = json_array_get_element(stops, i);
        const char *value = element && JSON_NODE_HOLDS_VALUE(element)
            ? json_node_get_string(element) : NULL;
        if (!value || !gdk_rgba_parse(&out->stops[i], value)) {
            g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                        "%s.stops[%u] is not a valid CSS colour", key, i);
            return FALSE;
        }
    }

    out->angle = json_object_has_member(gradient, "angle")
        ? json_object_get_double_member(gradient, "angle") : 180.0;
    out->angle = fmod(out->angle, 360.0);
    if (out->angle < 0.0)
        out->angle += 360.0;
    out->defined = TRUE;
    return TRUE;
}

static Theme *parse_theme(const char *data, gsize length,
                          const char *source, GError **error)
{
    g_autoptr(JsonParser) parser = json_parser_new();
    if (!json_parser_load_from_data(parser, data, length, error))
        return NULL;
    JsonNode *root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                    "%s does not contain a JSON object", source);
        return NULL;
    }
    JsonObject *object = json_node_get_object(root);
    if (!json_object_has_member(object, "id") ||
        !json_object_has_member(object, "name") ||
        !json_object_has_member(object, "colors")) {
        g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                    "%s needs id, name and colors", source);
        return NULL;
    }

    const char *id = json_object_get_string_member(object, "id");
    const char *name = json_object_get_string_member(object, "name");
    if (!id || !*id || !name || !*name) {
        g_set_error(error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                    "%s has an empty id or name", source);
        return NULL;
    }

    Theme *theme = g_new0(Theme, 1);
    theme->id = g_strdup(id);
    theme->name = g_strdup(name);
    if (json_object_has_member(object, "description"))
        theme->description = g_strdup(json_object_get_string_member(object, "description"));
    theme->opacity = json_object_has_member(object, "opacity")
        ? json_object_get_double_member(object, "opacity") : DEFAULT_OPACITY;
    theme->opacity = CLAMP(theme->opacity, OPACITY_MIN, OPACITY_MAX);

    JsonObject *colors = json_object_get_object_member(object, "colors");
    ThemePalette *palette = &theme->colors;
    gboolean ok =
        color_from_object(colors, "background", &palette->background, error) &&
        color_from_object(colors, "background-alt", &palette->background_alt, error) &&
        color_from_object(colors, "surface", &palette->surface, error) &&
        color_from_object(colors, "surface-alt", &palette->surface_alt, error) &&
        color_from_object(colors, "popover", &palette->popover, error) &&
        color_from_object(colors, "input", &palette->input, error) &&
        color_from_object(colors, "foreground", &palette->foreground, error) &&
        color_from_object(colors, "muted", &palette->muted, error) &&
        color_from_object(colors, "accent", &palette->accent, error) &&
        color_from_object(colors, "accent-bright", &palette->accent_bright, error) &&
        color_from_object(colors, "accent-dim", &palette->accent_dim, error) &&
        color_from_object(colors, "accent-text", &palette->accent_text, error) &&
        color_from_object(colors, "success", &palette->success, error) &&
        color_from_object(colors, "warning", &palette->warning, error) &&
        color_from_object(colors, "danger", &palette->danger, error) &&
        color_from_object(colors, "info", &palette->info, error);
    if (!ok) {
        theme_free(theme);
        return NULL;
    }

    /* Editor colours are optional.  Derive them from the UI palette so
     * a user theme only needs the main colours to look coherent. */
    JsonObject *editor = json_object_has_member(object, "editor")
        ? json_object_get_object_member(object, "editor") : NULL;
    palette->editor_background = palette->surface;
    palette->editor_foreground = palette->foreground;
    palette->editor_muted = palette->muted;
    palette->editor_line = mix_color(&palette->surface, &palette->foreground, 0.06);
    palette->editor_selection = mix_color(&palette->surface, &palette->accent, 0.35);
    palette->syntax_keyword = palette->accent;
    palette->syntax_string = palette->success;
    palette->syntax_number = palette->warning;
    palette->syntax_function = palette->info;
    palette->syntax_error = palette->danger;

    ok =
        optional_color(editor, "background", &palette->editor_background,
                       &palette->surface, error) &&
        optional_color(editor, "foreground", &palette->editor_foreground,
                       &palette->foreground, error) &&
        optional_color(editor, "muted", &palette->editor_muted,
                       &palette->muted, error) &&
        optional_color(editor, "line", &palette->editor_line,
                       &palette->editor_line, error) &&
        optional_color(editor, "selection", &palette->editor_selection,
                       &palette->editor_selection, error) &&
        optional_color(editor, "keyword", &palette->syntax_keyword,
                       &palette->syntax_keyword, error) &&
        optional_color(editor, "string", &palette->syntax_string,
                       &palette->syntax_string, error) &&
        optional_color(editor, "number", &palette->syntax_number,
                       &palette->syntax_number, error) &&
        optional_color(editor, "function", &palette->syntax_function,
                       &palette->syntax_function, error) &&
        optional_color(editor, "error", &palette->syntax_error,
                       &palette->syntax_error, error);
    if (!ok ||
        !gradient_from_object(object, "gradient", &theme->window_gradient, error) ||
        !gradient_from_object(object, "accent-gradient", &theme->accent_gradient, error)) {
        theme_free(theme);
        return NULL;
    }
    return theme;
}

static void add_theme(GPtrArray *themes, GHashTable *index, Theme *theme)
{
    gpointer existing = g_hash_table_lookup(index, theme->id);
    if (existing) {
        guint position = GPOINTER_TO_UINT(existing) - 1;
        theme_free(g_ptr_array_index(themes, position));
        g_ptr_array_index(themes, position) = theme;
        return;
    }
    guint position = themes->len;
    g_hash_table_insert(index, g_strdup(theme->id), GUINT_TO_POINTER(position + 1));
    g_ptr_array_add(themes, theme);
}

static void load_theme_data(GPtrArray *themes, GHashTable *index,
                            const char *data, gsize length, const char *source)
{
    g_autoptr(GError) error = NULL;
    Theme *theme = parse_theme(data, length, source, &error);
    if (!theme) {
        g_warning("Skipping theme %s: %s", source, error ? error->message : "unknown error");
        return;
    }
    add_theme(themes, index, theme);
}

static void load_resource_themes(GPtrArray *themes, GHashTable *index)
{
    g_autoptr(GError) error = NULL;
    g_auto(GStrv) children = g_resources_enumerate_children(
        RESOURCE_THEME_DIR, G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
    if (!children) {
        g_warning("Could not read bundled themes: %s", error->message);
        return;
    }
    for (char **child = children; *child; ++child) {
        if (!g_str_has_suffix(*child, ".json"))
            continue;
        g_autofree char *path = g_build_filename(RESOURCE_THEME_DIR, *child, NULL);
        g_autoptr(GError) lookup_error = NULL;
        g_autoptr(GBytes) bytes = g_resources_lookup_data(path, G_RESOURCE_LOOKUP_FLAGS_NONE,
                                                          &lookup_error);
        if (!bytes) {
            g_warning("Could not read %s: %s", path, lookup_error->message);
            continue;
        }
        gsize length = 0;
        const char *data = g_bytes_get_data(bytes, &length);
        load_theme_data(themes, index, data, length, path);
    }
}

static void load_directory(GPtrArray *themes, GHashTable *index, const char *directory)
{
    g_autoptr(GDir) dir = g_dir_open(directory, 0, NULL);
    if (!dir)
        return;
    const char *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (name[0] == '.' || !g_str_has_suffix(name, ".json"))
            continue;
        g_autofree char *path = g_build_filename(directory, name, NULL);
        g_autofree char *data = NULL;
        gsize length = 0;
        g_autoptr(GError) error = NULL;
        if (!g_file_get_contents(path, &data, &length, &error)) {
            g_warning("Could not read user theme %s: %s", path, error->message);
            continue;
        }
        load_theme_data(themes, index, data, length, path);
    }
}

static gint compare_themes(gconstpointer a, gconstpointer b)
{
    const Theme *left = *(Theme * const *)a;
    const Theme *right = *(Theme * const *)b;
    return g_utf8_collate(theme_get_name(left), theme_get_name(right));
}

GPtrArray *theme_list_load(void)
{
    GPtrArray *themes = g_ptr_array_new_with_free_func((GDestroyNotify)theme_free);
    GHashTable *index = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    load_resource_themes(themes, index);

    /* Later directories win, so user configuration overrides data and
     * system themes while bundled themes are the fallback. */
    const char *const *system_dirs = g_get_system_data_dirs();
    for (const char *const *dir = system_dirs; dir && *dir; ++dir) {
        g_autofree char *path = g_build_filename(*dir, "relapsegtk", "themes", NULL);
        load_directory(themes, index, path);
    }
    g_autofree char *data = g_build_filename(g_get_user_data_dir(),
                                             "relapsegtk", "themes", NULL);
    load_directory(themes, index, data);
    g_autofree char *config = g_build_filename(g_get_user_config_dir(),
                                               "relapsegtk", "themes", NULL);
    load_directory(themes, index, config);

    g_hash_table_unref(index);
    g_ptr_array_sort(themes, compare_themes);
    return themes;
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

static void append_color(GString *css, const char *name,
                         const GdkRGBA *color, double alpha)
{
    g_string_append_printf(css, "    --%s: ", name);
    append_rgba(css, color, alpha);
    g_string_append(css, ";\n");
}

typedef struct {
    const GdkRGBA *color;
    double position;
} FallbackStop;

static void append_gradient(GString *css, const char *name,
                            const ThemeGradient *gradient,
                            const FallbackStop *fallback, guint fallback_count,
                            double alpha)
{
    g_string_append_printf(css, "    --%s: linear-gradient(", name);
    if (gradient->defined) {
        g_string_append_printf(css, "%.0fdeg, ", gradient->angle);
        for (guint i = 0; i < gradient->n_stops; ++i) {
            double position = 100.0 * i / (gradient->n_stops - 1);
            if (i)
                g_string_append(css, ", ");
            append_rgba(css, &gradient->stops[i], alpha);
            g_string_append_printf(css, " %.0f%%", position);
        }
    } else {
        g_string_append(css, "180deg, ");
        for (guint i = 0; i < fallback_count; ++i) {
            if (i)
                g_string_append(css, ", ");
            append_rgba(css, fallback[i].color, alpha);
            g_string_append_printf(css, " %.0f%%", fallback[i].position);
        }
    }
    g_string_append(css, ");\n");
}

char *theme_build_css(const Theme *theme, double opacity)
{
    g_return_val_if_fail(theme != NULL, NULL);
    const ThemePalette *p = &theme->colors;
    double o = CLAMP(opacity, OPACITY_MIN, OPACITY_MAX);

    GdkRGBA ok_bright = mix_color(&p->success, &p->foreground, 0.45);
    GdkRGBA black = {.red = 0, .green = 0, .blue = 0, .alpha = 1};

    GString *css = g_string_new("/* Generated from the selected theme; do not edit generated CSS. */\n"
                                ".relapse-ui,\n.relapse-ui * {\n");
    append_color(css, "glass", &p->foreground, 0.035);
    append_color(css, "glass-soft", &p->foreground, 0.015);
    append_color(css, "glass-strong", &p->foreground, 0.070);
    append_color(css, "glass-press", &p->foreground, 0.025);
    append_color(css, "hairline", &p->foreground, 0.090);
    append_color(css, "hairline-soft", &p->foreground, 0.050);
    append_color(css, "highlight", &p->foreground, 0.300);
    append_color(css, "scrollbar", &p->foreground, 0.160);

    append_color(css, "text", &p->foreground, 1.0);
    append_color(css, "muted", &p->muted, 1.0);
    append_color(css, "accent", &p->accent, 1.0);
    append_color(css, "accent-bright", &p->accent_bright, 1.0);
    append_color(css, "accent-dim", &p->accent_dim, 1.0);
    append_color(css, "accent-text", &p->accent_text, 1.0);
    append_color(css, "accent-soft", &p->accent, 0.130);
    append_color(css, "accent-soft-strong", &p->accent, 0.300);
    append_color(css, "accent-line-faint", &p->accent, 0.160);
    append_color(css, "accent-line-soft", &p->accent, 0.240);
    append_color(css, "accent-line", &p->accent, 0.350);
    append_color(css, "accent-line-strong", &p->accent, 0.400);
    append_color(css, "accent-glow-soft", &p->accent_bright, 0.140);
    append_color(css, "accent-glow-medium", &p->accent_bright, 0.200);
    append_color(css, "accent-glow", &p->accent_bright, 0.450);
    append_color(css, "window-glow", &p->accent, 0.140);
    append_color(css, "selection", &p->accent, 0.300);

    append_color(css, "ok", &p->success, 1.0);
    append_color(css, "ok-bright", &ok_bright, 1.0);
    append_color(css, "ok-soft", &p->success, 0.200);
    append_color(css, "warning", &p->warning, 1.0);
    append_color(css, "danger", &p->danger, 1.0);
    append_color(css, "danger-soft", &p->danger, 0.100);
    append_color(css, "info", &p->info, 1.0);

    /* Background layers are the only values that follow the opacity
     * slider.  Foreground text and accents always stay fully opaque. */
    append_color(css, "window-top", &p->background, o);
    append_color(css, "window-mid", &p->background_alt, o);
    append_color(css, "window-bottom", &p->surface, o);
    append_color(css, "surface", &p->surface, o);
    append_color(css, "surface-alt", &p->surface_alt, o);
    append_color(css, "output-bg", &p->surface_alt, o);
    append_color(css, "popover-bg", &p->popover, o);
    append_color(css, "input-bg", &p->input, o * 0.85);
    append_color(css, "input-bg-soft", &p->input, o * 0.55);

    const FallbackStop window_stops[] = {
        {&p->background, 0.0},
        {&p->background_alt, 55.0},
        {&p->surface, 100.0},
    };
    const FallbackStop accent_stops[] = {
        {&p->accent, 0.0},
        {&p->accent_bright, 100.0},
    };
    append_gradient(css, "window-gradient", &theme->window_gradient,
                    window_stops, G_N_ELEMENTS(window_stops), o);
    append_gradient(css, "accent-gradient", &theme->accent_gradient,
                    accent_stops, G_N_ELEMENTS(accent_stops), 1.0);

    append_color(css, "shadow-strong", &black, 0.550);
    append_color(css, "shadow-soft", &black, 0.400);

    g_string_append(css, "}\n");
    return g_string_free(css, FALSE);
}

char *theme_build_editor_scheme_xml(const Theme *theme)
{
    g_return_val_if_fail(theme != NULL, NULL);
    const ThemePalette *p = &theme->colors;
    g_autofree char *scheme_id = theme_editor_scheme_id(theme);
    g_autofree char *safe_name = g_markup_escape_text(theme->name, -1);
    g_autofree char *safe_description = g_markup_escape_text(
        theme->description ? theme->description : "RelapseGtk editor palette", -1);

    GString *xml = g_string_new("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    g_string_append_printf(xml,
        "<style-scheme id=\"%s\" name=\"%s\" version=\"1.0\">\n"
        "  <description>%s</description>\n",
        scheme_id, safe_name, safe_description);

    struct {
        const char *name;
        const GdkRGBA *color;
    } colors[] = {
        {"background", &p->editor_background},
        {"foreground", &p->editor_foreground},
        {"muted", &p->editor_muted},
        {"line", &p->editor_line},
        {"selection", &p->editor_selection},
        {"keyword", &p->syntax_keyword},
        {"string", &p->syntax_string},
        {"number", &p->syntax_number},
        {"function", &p->syntax_function},
        {"error", &p->syntax_error},
    };
    for (guint i = 0; i < G_N_ELEMENTS(colors); ++i) {
        g_autofree char *hex = theme_color_to_hex(colors[i].color);
        g_string_append_printf(xml, "  <color name=\"%s\" value=\"%s\"/>\n",
                               colors[i].name, hex);
    }

    g_string_append(xml,
        "\n"
        "  <style name=\"text\" foreground=\"foreground\" background=\"background\"/>\n"
        "  <style name=\"selection\" foreground=\"foreground\" background=\"selection\"/>\n"
        "  <style name=\"selection-unfocused\" foreground=\"foreground\" background=\"selection\"/>\n"
        "  <style name=\"cursor\" foreground=\"foreground\"/>\n"
        "  <style name=\"current-line\" background=\"line\"/>\n"
        "  <style name=\"line-numbers\" foreground=\"muted\" background=\"background\"/>\n"
        "  <style name=\"line-numbers-border\" background=\"background\"/>\n"
        "  <style name=\"current-line-number\" foreground=\"foreground\" background=\"line\" bold=\"true\"/>\n"
        "  <style name=\"bracket-match\" foreground=\"foreground\" background=\"selection\" bold=\"true\"/>\n"
        "  <style name=\"bracket-mismatch\" foreground=\"error\" underline=\"error\"/>\n"
        "  <style name=\"draw-spaces\" foreground=\"muted\"/>\n"
        "  <style name=\"search-match\" foreground=\"background\" background=\"number\"/>\n"
        "\n"
        "  <style name=\"def:comment\" foreground=\"muted\"/>\n"
        "  <style name=\"def:keyword\" foreground=\"keyword\"/>\n"
        "  <style name=\"def:statement\" foreground=\"keyword\"/>\n"
        "  <style name=\"def:type\" foreground=\"function\"/>\n"
        "  <style name=\"def:function\" foreground=\"function\"/>\n"
        "  <style name=\"def:string\" foreground=\"string\"/>\n"
        "  <style name=\"def:special-char\" foreground=\"number\"/>\n"
        "  <style name=\"def:constant\" foreground=\"number\"/>\n"
        "  <style name=\"def:special-constant\" foreground=\"number\"/>\n"
        "  <style name=\"def:boolean\" foreground=\"number\"/>\n"
        "  <style name=\"def:number\" foreground=\"number\"/>\n"
        "  <style name=\"def:decimal\" foreground=\"number\"/>\n"
        "  <style name=\"def:base-n-integer\" foreground=\"number\"/>\n"
        "  <style name=\"def:floating-point\" foreground=\"number\"/>\n"
        "  <style name=\"def:error\" foreground=\"error\" underline=\"error\"/>\n"
        "</style-scheme>\n");
    return g_string_free(xml, FALSE);
}

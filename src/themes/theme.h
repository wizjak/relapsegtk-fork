#pragma once

#include <gdk/gdk.h>

/*
 * A theme is a plain palette loaded from a JSON file.  Nothing in this
 * module picks a colour of its own: every value below comes from the
 * theme file, and the helper functions only combine those values.
 *
 * Built-in themes live in resources/themes/.  Users can drop extra
 * *.json files in $XDG_CONFIG_HOME/relapsegtk/themes/ (usually
 * ~/.config/relapsegtk/themes/) and override a bundled theme by reusing
 * its "id".
 */
typedef struct {
    GdkRGBA background;
    GdkRGBA background_alt;
    GdkRGBA surface;
    GdkRGBA surface_alt;
    GdkRGBA popover;
    GdkRGBA input;
    GdkRGBA foreground;
    GdkRGBA muted;
    GdkRGBA accent;
    GdkRGBA accent_bright;
    GdkRGBA accent_dim;
    GdkRGBA accent_text;
    GdkRGBA success;
    GdkRGBA warning;
    GdkRGBA danger;
    GdkRGBA info;

    /* GtkSourceView palette.  Missing "editor" keys are derived from the
     * colours above so a compact theme file is still valid. */
    GdkRGBA editor_background;
    GdkRGBA editor_foreground;
    GdkRGBA editor_muted;
    GdkRGBA editor_line;
    GdkRGBA editor_selection;
    GdkRGBA syntax_keyword;
    GdkRGBA syntax_string;
    GdkRGBA syntax_number;
    GdkRGBA syntax_function;
    GdkRGBA syntax_error;
} ThemePalette;

typedef struct {
    GdkRGBA *stops;
    guint n_stops;
    double angle;
    gboolean defined;
} ThemeGradient;

typedef struct {
    char *id;
    char *name;
    char *description;
    double opacity;
    ThemePalette colors;

    /* Optional gradients.  When absent, the window gradient is derived
     * from background/background-alt/surface and the accent gradient
     * from accent/accent-bright. */
    ThemeGradient window_gradient;
    ThemeGradient accent_gradient;
} Theme;

/* Returns a GPtrArray of Theme* sorted by display name.  The array owns
 * the themes and frees them with theme_free(). */
GPtrArray *theme_list_load(void);
void theme_free(Theme *theme);

const char *theme_get_id(const Theme *theme);
const char *theme_get_name(const Theme *theme);
const char *theme_get_description(const Theme *theme);
double theme_get_opacity(const Theme *theme);
const ThemePalette *theme_get_palette(const Theme *theme);

/* CSS custom properties consumed by resources/style.css. */
char *theme_build_css(const Theme *theme, double opacity);

/* A GtkSourceView XML style scheme generated from the same palette. */
char *theme_build_editor_scheme_xml(const Theme *theme);
char *theme_editor_scheme_id(const Theme *theme);

/* Small colour helpers shared with the renderer helpers. */
char *theme_color_to_hex(const GdkRGBA *color);

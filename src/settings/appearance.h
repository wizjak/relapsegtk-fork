#pragma once

#include <glib.h>

#define APPEARANCE_THEME_DEFAULT "aurora"
#define APPEARANCE_TITLE_DEFAULT "RelapseGtk"
#define APPEARANCE_RADIUS_DEFAULT 12
#define APPEARANCE_RADIUS_MAX 24
#define APPEARANCE_BACKGROUND_FIT_DEFAULT "cover"
#define APPEARANCE_BACKGROUND_OPACITY_DEFAULT 1.0
#define APPEARANCE_BACKGROUND_DIM_DEFAULT 0.35
#define APPEARANCE_BACKGROUND_DIM_MAX 0.80

typedef struct {
    char *theme;
    /* 0 means "use the theme file's opacity". */
    double opacity;
    gboolean blur;

    /* Optional background image painted behind the theme tint.  The path is
     * kept even when the file is missing so the user can move it back. */
    char *background_image;
    /* "cover" or "contain". */
    char *background_fit;
    /* 0..1 alpha of the image itself. */
    double background_opacity;
    /* 0..APPEARANCE_BACKGROUND_DIM_MAX black overlay for legibility. */
    double background_dim;

    /* 0..APPEARANCE_RADIUS_MAX pixels. */
    int window_radius;
    /* Static window title; empty means APPEARANCE_TITLE_DEFAULT. */
    char *window_title;
} AppearanceSettings;

AppearanceSettings appearance_settings_defaults(void);
gboolean appearance_settings_load(AppearanceSettings *settings, GError **error);
gboolean appearance_settings_save(const AppearanceSettings *settings, GError **error);
void appearance_settings_copy(AppearanceSettings *dest, const AppearanceSettings *src);
void appearance_settings_clear(AppearanceSettings *settings);

#include "appearance.h"

#include "store.h"

enum { APPEARANCE_OPACITY_MIN = 30, APPEARANCE_OPACITY_MAX = 100 };

static void format_double(char *buffer, gsize size, double value)
{
    g_ascii_formatd(buffer, size, "%.2f", value);
}

static const char *valid_fit(const char *fit)
{
    return g_strcmp0(fit, "contain") == 0 ? "contain" : "cover";
}

AppearanceSettings appearance_settings_defaults(void)
{
    return (AppearanceSettings){
        .theme = g_strdup(APPEARANCE_THEME_DEFAULT),
        .opacity = 0.0,
        .blur = TRUE,
        .background_image = NULL,
        .background_fit = g_strdup(APPEARANCE_BACKGROUND_FIT_DEFAULT),
        .background_opacity = APPEARANCE_BACKGROUND_OPACITY_DEFAULT,
        .background_dim = APPEARANCE_BACKGROUND_DIM_DEFAULT,
        .window_radius = APPEARANCE_RADIUS_DEFAULT,
        .window_title = g_strdup(APPEARANCE_TITLE_DEFAULT),
    };
}

void appearance_settings_clear(AppearanceSettings *settings)
{
    if (!settings)
        return;
    g_free(settings->theme);
    g_free(settings->background_image);
    g_free(settings->background_fit);
    g_free(settings->window_title);
    *settings = (AppearanceSettings){0};
}

void appearance_settings_copy(AppearanceSettings *dest, const AppearanceSettings *src)
{
    if (!dest || !src || dest == src)
        return;
    appearance_settings_clear(dest);
    *dest = (AppearanceSettings){
        .theme = g_strdup(src->theme),
        .opacity = src->opacity,
        .blur = src->blur,
        .background_image = g_strdup(src->background_image),
        .background_fit = g_strdup(valid_fit(src->background_fit)),
        .background_opacity = CLAMP(src->background_opacity, 0.0, 1.0),
        .background_dim = CLAMP(src->background_dim, 0.0, APPEARANCE_BACKGROUND_DIM_MAX),
        .window_radius = CLAMP(src->window_radius, 0, APPEARANCE_RADIUS_MAX),
        .window_title = g_strdup(src->window_title ? src->window_title : ""),
    };
}

gboolean appearance_settings_load(AppearanceSettings *settings, GError **error)
{
    *settings = appearance_settings_defaults();
    g_autoptr(GKeyFile) file = store_load(FALSE, error);
    if (!file)
        return FALSE;

    g_autofree char *theme = g_key_file_get_string(file, "Appearance", "theme", NULL);
    if (theme && *theme) {
        g_free(settings->theme);
        settings->theme = g_steal_pointer(&theme);
    }

    g_autoptr(GError) failure = NULL;
    double opacity = g_key_file_get_double(file, "Appearance", "opacity", &failure);
    if (!failure && opacity >= APPEARANCE_OPACITY_MIN / 100.0 &&
        opacity <= APPEARANCE_OPACITY_MAX / 100.0)
        settings->opacity = opacity;
    g_clear_error(&failure);

    if (g_key_file_has_key(file, "Appearance", "blur", NULL))
        settings->blur = g_key_file_get_boolean(file, "Appearance", "blur", NULL);

    g_autofree char *image = g_key_file_get_string(file, "Appearance", "background_image", NULL);
    if (image && *image) {
        g_free(settings->background_image);
        settings->background_image = g_steal_pointer(&image);
    }

    g_autofree char *fit = g_key_file_get_string(file, "Appearance", "background_fit", NULL);
    if (fit && *fit) {
        g_free(settings->background_fit);
        settings->background_fit = g_strdup(valid_fit(fit));
    }

    double image_opacity = g_key_file_get_double(file, "Appearance", "background_opacity", &failure);
    if (!failure)
        settings->background_opacity = CLAMP(image_opacity, 0.0, 1.0);
    g_clear_error(&failure);

    double dim = g_key_file_get_double(file, "Appearance", "background_dim", &failure);
    if (!failure)
        settings->background_dim = CLAMP(dim, 0.0, APPEARANCE_BACKGROUND_DIM_MAX);
    g_clear_error(&failure);

    int radius = g_key_file_get_integer(file, "Appearance", "window_radius", &failure);
    if (!failure)
        settings->window_radius = CLAMP(radius, 0, APPEARANCE_RADIUS_MAX);
    g_clear_error(&failure);

    g_autofree char *title = g_key_file_get_string(file, "Appearance", "window_title", NULL);
    if (title && *title) {
        g_free(settings->window_title);
        settings->window_title = g_steal_pointer(&title);
    }
    return TRUE;
}

gboolean appearance_settings_save(const AppearanceSettings *settings, GError **error)
{
    if (!settings || !settings->theme || !*settings->theme) {
        g_set_error_literal(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                            "Choose a color scheme before saving appearance settings.");
        return FALSE;
    }

    double opacity = CLAMP(settings->opacity, APPEARANCE_OPACITY_MIN / 100.0,
                           APPEARANCE_OPACITY_MAX / 100.0);
    double image_opacity = CLAMP(settings->background_opacity, 0.0, 1.0);
    double dim = CLAMP(settings->background_dim, 0.0, APPEARANCE_BACKGROUND_DIM_MAX);
    int radius = CLAMP(settings->window_radius, 0, APPEARANCE_RADIUS_MAX);
    const char *fit = valid_fit(settings->background_fit);
    const char *title = settings->window_title && *settings->window_title
        ? settings->window_title : APPEARANCE_TITLE_DEFAULT;

    g_autoptr(GKeyFile) file = store_load(TRUE, error);
    if (!file)
        return FALSE;

    /* Write short, locale-independent decimals so the file stays readable
     * instead of storing the slider's full binary representation. */
    char opacity_text[G_ASCII_DTOSTR_BUF_SIZE];
    char image_opacity_text[G_ASCII_DTOSTR_BUF_SIZE];
    char dim_text[G_ASCII_DTOSTR_BUF_SIZE];
    format_double(opacity_text, sizeof opacity_text, opacity);
    format_double(image_opacity_text, sizeof image_opacity_text, image_opacity);
    format_double(dim_text, sizeof dim_text, dim);

    g_key_file_set_string(file, "Appearance", "theme", settings->theme);
    g_key_file_set_string(file, "Appearance", "opacity", opacity_text);
    g_key_file_set_boolean(file, "Appearance", "blur", settings->blur);
    g_key_file_set_string(file, "Appearance", "background_image",
                          settings->background_image ? settings->background_image : "");
    g_key_file_set_string(file, "Appearance", "background_fit", fit);
    g_key_file_set_string(file, "Appearance", "background_opacity", image_opacity_text);
    g_key_file_set_string(file, "Appearance", "background_dim", dim_text);
    g_key_file_set_integer(file, "Appearance", "window_radius", radius);
    g_key_file_set_string(file, "Appearance", "window_title", title);
    return store_save(file, error);
}

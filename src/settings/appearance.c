#include "appearance.h"
#include "store.h"

#include <string.h>

gboolean appearance_accent_valid(const char *accent)
{
    if (!accent || accent[0] != '#' || strlen(accent) != 7)
        return FALSE;
    for (guint i = 1; i < 7; ++i)
        if (!g_ascii_isxdigit(accent[i]))
            return FALSE;
    return TRUE;
}

gboolean appearance_settings_load(char **accent, gboolean *shader_strip,
                                  gboolean *topographic, GError **error)
{
    *accent = g_strdup(APPEARANCE_DEFAULT_ACCENT);
    *shader_strip = APPEARANCE_DEFAULT_SHADER_STRIP;
    *topographic = APPEARANCE_DEFAULT_TOPOGRAPHIC;
    g_autoptr(GKeyFile) file = store_load(FALSE, error);
    if (!file)
        return FALSE;
    g_autofree char *value = g_key_file_get_string(file, "Appearance", "accent", NULL);
    if (appearance_accent_valid(value)) {
        g_free(*accent);
        *accent = g_steal_pointer(&value);
    }
    *shader_strip = g_key_file_get_boolean(file, "Appearance", "shader_strip", NULL);
    *topographic = g_key_file_get_boolean(file, "Appearance", "topographic_background", NULL);
    return TRUE;
}

gboolean appearance_settings_save(const char *accent, gboolean shader_strip,
                                  gboolean topographic, GError **error)
{
    if (!appearance_accent_valid(accent)) {
        g_set_error_literal(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                            "Choose a valid accent color.");
        return FALSE;
    }
    g_autoptr(GKeyFile) file = store_load(TRUE, error);
    if (!file)
        return FALSE;
    g_key_file_set_string(file, "Appearance", "accent", accent);
    g_key_file_set_boolean(file, "Appearance", "shader_strip", shader_strip);
    g_key_file_set_boolean(file, "Appearance", "topographic_background", topographic);
    return store_save(file, error);
}

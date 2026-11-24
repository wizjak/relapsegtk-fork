#pragma once

#include <glib.h>

#define APPEARANCE_DEFAULT_ACCENT "#34e2a0"
#define APPEARANCE_DEFAULT_SHADER_STRIP TRUE
#define APPEARANCE_DEFAULT_TOPOGRAPHIC FALSE

gboolean appearance_accent_valid(const char *accent);
gboolean appearance_settings_load(char **accent, gboolean *shader_strip,
                                  gboolean *topographic, GError **error);
gboolean appearance_settings_save(const char *accent, gboolean shader_strip,
                                  gboolean topographic, GError **error);

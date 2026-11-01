#pragma once

#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>
#include "themes/theme.h"
#include "settings/appearance.h"

#define STYLE_OPACITY_MIN 0.30
#define STYLE_OPACITY_MAX 1.00

void style_install(GdkDisplay *display);
void style_set_editor_font_size(GdkDisplay *display, int size);

/* Theme discovery and application.  The theme list is owned by the style
 * module and remains valid for the lifetime of the display. */
const GPtrArray *style_theme_list(void);
const char *style_default_theme_id(void);
const Theme *style_current_theme(void);
const char *style_current_theme_id(void);
double style_current_opacity(void);

/* Applies every palette token to the UI, regenerates the GtkSourceView
 * scheme, and remembers the selection.  Pass opacity <= 0 to use the
 * opacity declared by the theme file. */
gboolean style_set_theme(const char *theme_id, double opacity);

/* Applies window-only overrides (corner radius, background image layers)
 * on top of the active theme.  Call after style_set_theme(). */
void style_set_appearance(const AppearanceSettings *settings);

/* Helpers for widgets that own their own source buffers or text tags. */
GtkSourceStyleScheme *style_editor_scheme(void);
void style_apply_text_tags(GtkTextBuffer *buffer);

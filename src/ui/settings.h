#pragma once

#include <gtk/gtk.h>
#include "settings/editor.h"

typedef void (*SettingsChangedFunc)(const EditorSettings *settings, gpointer user_data);
typedef void (*RestoreTabsChangedFunc)(gboolean enabled, gpointer user_data);
typedef void (*OutputChangedFunc)(gboolean enabled, gpointer user_data);
typedef gboolean (*LauncherChangedFunc)(const char *path, GError **error, gpointer user_data);
typedef gboolean (*ScriptsChangedFunc)(const char *path, GError **error, gpointer user_data);
typedef void (*AccentChangedFunc)(const char *accent, gpointer user_data);
typedef void (*ShaderStripChangedFunc)(gboolean enabled, gpointer user_data);
typedef void (*TopographicChangedFunc)(gboolean enabled, gpointer user_data);

GtkWidget *settings_new(const EditorSettings *settings, SettingsChangedFunc changed,
                       RestoreTabsChangedFunc restore_changed, OutputChangedFunc output_changed,
                       LauncherChangedFunc launcher_changed, ScriptsChangedFunc scripts_changed,
                       AccentChangedFunc accent_changed, ShaderStripChangedFunc shader_strip_changed,
                       TopographicChangedFunc topographic_changed, gpointer user_data);
void settings_set_editor(GtkWidget *page, const EditorSettings *settings);
void settings_set_restore_tabs(GtkWidget *page, gboolean enabled);
void settings_set_output(GtkWidget *page, gboolean enabled);
void settings_set_launcher(GtkWidget *page, const char *path);
void settings_set_scripts_path(GtkWidget *page, const char *path);
void settings_set_accent(GtkWidget *page, const char *accent);
void settings_set_shader_strip(GtkWidget *page, gboolean enabled);
void settings_set_topographic(GtkWidget *page, gboolean enabled);
void settings_shutdown(GtkWidget *page);

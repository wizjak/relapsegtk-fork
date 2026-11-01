#pragma once

#include <gio/gio.h>

gboolean profile_name_valid(const char *name);
gboolean profile_create(const char *name, GError **error);
gboolean profile_rename(const char *name, const char *replacement, GError **error);
char **profiles_list(void);
char **profile_sockets_list(const char *directory);
char *profile_socket_name(const char *path);
gboolean profile_is_running(const char *name);
GSubprocess *profile_launch(const char *launcher, const char *name, GError **error);

/* Returns TRUE when any process in /proc matches one of the executable
 * basenames or Linux comm names.  A NULL-terminated list is expected. */
gboolean processes_running(const char *const *names);

/* Launches the configured launcher with no arguments, detached from the UI. */
GSubprocess *launcher_launch(const char *launcher, GError **error);

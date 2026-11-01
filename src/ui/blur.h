#pragma once

#include <gtk/gtk.h>

/* Blur is rendered by GTK 4.24+ through the Wayland ext-background-effect-v1
 * protocol.  Compositors without that protocol simply report "unsupported". */
gboolean blur_supported(void);

/* Toggles the CSS class that enables GTK's window backdrop blur. */
void blur_set_enabled(GtkWindow *window, gboolean enabled);

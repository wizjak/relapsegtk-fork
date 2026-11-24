#pragma once

#include <gtk/gtk.h>

GtkWidget *topographic_background_new(void);
void topographic_background_set_accent(GtkWidget *background, const GdkRGBA *accent);

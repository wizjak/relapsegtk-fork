#pragma once

#include <gtk/gtk.h>

GtkWidget *shader_strip_new(void);
void shader_strip_set_accent(GtkWidget *strip, const GdkRGBA *accent);

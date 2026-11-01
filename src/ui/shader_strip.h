#pragma once

#include <gtk/gtk.h>

GtkWidget *shader_strip_new(void);

/* 0 = resting (dim, slow sweep feel), 1 = fully energised (bright accent). */
void shader_strip_set_energy(GtkWidget *strip, float energy);

/* Colours come from the active theme so the strip follows the palette. */
void shader_strip_set_colors(GtkWidget *strip, const GdkRGBA *base,
                             const GdkRGBA *accent);

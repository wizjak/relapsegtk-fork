#pragma once

#include <gdk/gdk.h>

void style_install(GdkDisplay *display);
void style_set_editor_font_size(GdkDisplay *display, int size);
void style_set_accent(GdkDisplay *display, const char *accent);

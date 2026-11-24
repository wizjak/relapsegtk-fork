#include "style.h"

#include <gtk/gtk.h>
#include <string.h>

static void parsing_error(GtkCssProvider *provider, GtkCssSection *section,
                          const GError *error, gpointer user_data)
{
    (void)provider;
    (void)section;
    (void)user_data;
    g_error("Invalid bundled stylesheet: %s", error->message);
}

static GtkCssProvider *display_provider(GdkDisplay *display, const char *key,
                                        int priority)
{
    GtkCssProvider *provider = g_object_get_data(G_OBJECT(display), key);
    if (provider)
        return provider;
    provider = gtk_css_provider_new();
    g_signal_connect(provider, "parsing-error", G_CALLBACK(parsing_error), NULL);
    g_object_set_data_full(G_OBJECT(display), key, provider, g_object_unref);
    gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(provider),
                                               priority);
    return provider;
}

void style_install(GdkDisplay *display)
{
    GtkCssProvider *provider = gtk_css_provider_new();
    g_signal_connect(provider, "parsing-error", G_CALLBACK(parsing_error), NULL);
    gtk_css_provider_load_from_resource(provider, "/org/relapse/RelapseGtk/style.css");

    gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_USER + 1);
    g_object_unref(provider);
}

void style_set_editor_font_size(GdkDisplay *display, int size)
{
    GtkCssProvider *provider = display_provider(display, "editor-font-provider",
                                                GTK_STYLE_PROVIDER_PRIORITY_USER + 2);
    g_autofree char *css = g_strdup_printf(
        ".relapse-ui textview.code-editor, .relapse-ui textview.code-editor * { font-size: %dpx; }", size);
    gtk_css_provider_load_from_string(provider, css);
}

static void rgba_string(char *buffer, gsize size, const GdkRGBA *color, double alpha)
{
    char alpha_text[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_formatd(alpha_text, sizeof alpha_text, "%.3f", alpha);
    g_snprintf(buffer, size, "rgba(%d, %d, %d, %s)",
               (int)(color->red * 255.0 + 0.5),
               (int)(color->green * 255.0 + 0.5),
               (int)(color->blue * 255.0 + 0.5), alpha_text);
}

static GdkRGBA mix_toward(const GdkRGBA *color, double amount)
{
    return (GdkRGBA){
        .red = color->red * (1.0 - amount) + amount,
        .green = color->green * (1.0 - amount) + amount,
        .blue = color->blue * (1.0 - amount) + amount,
        .alpha = 1.0,
    };
}

void style_set_accent(GdkDisplay *display, const char *accent)
{
    GdkRGBA base;
    if (!accent || !gdk_rgba_parse(&base, accent))
        return;

    GdkRGBA strong = mix_toward(&base, 0.32);
    char hex[8];
    g_snprintf(hex, sizeof hex, "#%02x%02x%02x",
               (int)(base.red * 255.0 + 0.5),
               (int)(base.green * 255.0 + 0.5),
               (int)(base.blue * 255.0 + 0.5));
    char soft[64], dim[64], line[64], glow[64], strong_css[64];
    rgba_string(soft, sizeof soft, &base, 0.12);
    rgba_string(dim, sizeof dim, &base, 0.24);
    rgba_string(line, sizeof line, &base, 0.55);
    rgba_string(glow, sizeof glow, &base, 0.30);
    rgba_string(strong_css, sizeof strong_css, &strong, 1.0);

    g_autofree char *css = g_strdup_printf(
        ".relapse-ui, .relapse-ui * {\n"
        "  --relapse-accent: %s;\n"
        "  --relapse-accent-soft: %s;\n"
        "  --relapse-accent-dim: %s;\n"
        "  --relapse-accent-line: %s;\n"
        "  --relapse-accent-glow: %s;\n"
        "  --relapse-accent-strong: %s;\n"
        "  --relapse-accent-ink: #05090b;\n"
        "}\n", hex, soft, dim, line, glow, strong_css);

    GtkCssProvider *provider = display_provider(display, "accent-provider",
                                                GTK_STYLE_PROVIDER_PRIORITY_USER + 3);
    gtk_css_provider_load_from_string(provider, css);
}

#include "blur.h"

#if defined(RELAPSEGTK_HAVE_WAYLAND) && GTK_CHECK_VERSION(4, 24, 0)

#include <gdk/wayland/gdkwayland.h>
#include <string.h>
#include <wayland-client.h>

/* GTK 4.24 implements blur itself: the window's CSS `backdrop-filter` becomes
 * a blur region on Wayland's ext-background-effect-v1 protocol.  We only need
 * to know whether the compositor advertises the protocol and toggle the class.
 *
 * Whether blur is actually drawn is up to the compositor's blur effect.  Some
 * compositors (including KWin) only expose the blur capability when a specific
 * built-in effect is loaded, but third-party blur effects may still consume
 * the region, so support is based on the global rather than its capability. */

typedef struct {
    gboolean found;
} SupportQuery;

static GtkCssProvider *blur_provider;

static void support_global(void *data, struct wl_registry *registry, uint32_t name,
                           const char *interface, uint32_t version)
{
    (void)registry;
    (void)name;
    (void)version;
    SupportQuery *query = data;
    if (strcmp(interface, "ext_background_effect_manager_v1") == 0)
        query->found = TRUE;
}

static void support_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

gboolean blur_supported(void)
{
    static gboolean checked;
    static gboolean supported;
    if (checked)
        return supported;
    checked = TRUE;

    GdkDisplay *display = gdk_display_get_default();
    if (!display || !GDK_IS_WAYLAND_DISPLAY(display))
        return supported;
    struct wl_display *wl_display = gdk_wayland_display_get_wl_display(display);
    if (!wl_display)
        return supported;

    SupportQuery query = {0};
    struct wl_registry *registry = wl_display_get_registry(wl_display);
    if (!registry)
        return supported;
    static const struct wl_registry_listener listener = {
        .global = support_global,
        .global_remove = support_global_remove,
    };
    wl_registry_add_listener(registry, &listener, &query);
    wl_display_roundtrip(wl_display);
    wl_registry_destroy(registry);

    supported = query.found;
    if (supported && !blur_provider) {
        /* Keep the rule out of the base stylesheet so older GTK builds that
         * do not know `backdrop-filter` never have to parse it. */
        blur_provider = gtk_css_provider_new();
        gtk_css_provider_load_from_string(blur_provider,
            "window.relapse-ui.blurred { backdrop-filter: blur(32px); box-shadow: none; }");
        gtk_style_context_add_provider_for_display(display,
            GTK_STYLE_PROVIDER(blur_provider), GTK_STYLE_PROVIDER_PRIORITY_USER + 5);
    }
    return supported;
}

void blur_set_enabled(GtkWindow *window, gboolean enabled)
{
    if (enabled)
        gtk_widget_add_css_class(GTK_WIDGET(window), "blurred");
    else
        gtk_widget_remove_css_class(GTK_WIDGET(window), "blurred");
}

#else

gboolean blur_supported(void)
{
    return FALSE;
}

void blur_set_enabled(GtkWindow *window, gboolean enabled)
{
    (void)window;
    (void)enabled;
}

#endif

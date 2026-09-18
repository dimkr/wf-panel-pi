#include "item.h"

#include <math.h>
#include <string.h>

#include <libdbusmenu-gtk/dbusmenu-gtk.h>

#include "plugin.h"

/*----------------------------------------------------------------------------*/
/* Prototypes                                                                 */
/*----------------------------------------------------------------------------*/

static void name_and_obj_path (const char *service, char **name, char **path);
static GdkPixbuf *extract_pixbuf (GVariant *pixbuf_data);
static void on_long_press (GtkGesture *gesture, gdouble x, gdouble y, gpointer userdata);
static GtkGesture *detect_long_press (GtkWidget *target);
static void on_item_g_signal (GDBusProxy *proxy, const gchar *sender, const gchar *signal, GVariant *params, gpointer userdata);
static void on_item_proxy_ready (GObject *source, GAsyncResult *res, gpointer userdata);
static GVariant *get_item_property_variant (StatusNotifierItem *item, const char *name);
static gboolean get_item_property_bool (StatusNotifierItem *item, const char *name, gboolean default_value);
static char *get_item_property_string (StatusNotifierItem *item, const char *name);
static void init_widget (StatusNotifierItem *item);
static gboolean on_button_press_event (GtkWidget *widget, GdkEventButton *ev, gpointer userdata);
static gboolean on_button_release_event (GtkWidget *widget, GdkEventButton *ev, gpointer userdata);
static gboolean on_scroll_event (GtkWidget *widget, GdkEventScroll *ev, gpointer userdata);
static gboolean on_query_tooltip (GtkWidget *widget, gint x, gint y, gboolean keyboard_mode, GtkTooltip *tooltip, gpointer userdata);
static void setup_tooltip (StatusNotifierItem *item);
static void init_menu (StatusNotifierItem *item);
static void fetch_property (StatusNotifierItem *item, const char *property_name, void (*callback) (gpointer), gpointer callback_data);
static void handle_signal (StatusNotifierItem *item, const char *signal, GVariant *params);

/*----------------------------------------------------------------------------*/

static void name_and_obj_path (const char *service, char **name, char **path)
{
    const char *slash = strchr (service, '/');

    if (slash)
    {
        *name = g_strndup (service, slash - service);
        *path = g_strdup (slash);
    }
    else
    {
        *name = g_strdup (service);
        *path = g_strdup ("/StatusNotifierItem");
    }
}

static void free_pixel_data (guchar *pixels, gpointer data)
{
    g_free (pixels);
}

static GdkPixbuf *extract_pixbuf (GVariant *pixbuf_data)
{
    GVariantIter iter;
    gint32 width, height, best_width = 0, best_height = 0;
    GVariant *bytes_v;
    guchar *best_data = NULL;
    gsize best_len = 0;
    gboolean have = FALSE;
    gsize i;

    if (!pixbuf_data) return NULL;

    g_variant_iter_init (&iter, pixbuf_data);
    while (g_variant_iter_next (&iter, "(ii@ay)", &width, &height, &bytes_v))
    {
        gsize len;
        const guchar *data = (const guchar *) g_variant_get_fixed_array (bytes_v, &len, sizeof (guchar));
        gboolean is_max = FALSE;

        if (!have) is_max = TRUE;
        else if (width > best_width) is_max = TRUE;
        else if (width == best_width && height > best_height) is_max = TRUE;
        else if (width == best_width && height == best_height)
        {
            gsize minlen = len < best_len ? len : best_len;
            int cmp = memcmp (data, best_data, minlen);
            if (cmp > 0 || (cmp == 0 && len > best_len)) is_max = TRUE;
        }

        if (is_max)
        {
            g_free (best_data);
            best_data = (guchar *) g_memdup2 (data, len);
            best_len = len;
            best_width = width;
            best_height = height;
            have = TRUE;
        }

        g_variant_unref (bytes_v);
    }

    if (!have) return NULL;

    /* argb to rgba */
    for (i = 0; i + 3 < best_len; i += 4)
    {
        guchar alpha = best_data[i];
        best_data[i]     = best_data[i + 1];
        best_data[i + 1] = best_data[i + 2];
        best_data[i + 2] = best_data[i + 3];
        best_data[i + 3] = alpha;
    }

    return gdk_pixbuf_new_from_data (best_data, GDK_COLORSPACE_RGB, TRUE, 8, best_width, best_height,
        4 * best_width, free_pixel_data, NULL);
}

static void on_long_press (GtkGesture *gesture, gdouble x, gdouble y, gpointer userdata)
{
    pressed = PRESS_LONG;
}

static GtkGesture *detect_long_press (GtkWidget *target)
{
    GtkGesture *gesture = gtk_gesture_long_press_new (target);

    gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (gesture), GTK_PHASE_BUBBLE);
    g_signal_connect (gesture, "pressed", G_CALLBACK (on_long_press), NULL);
    gtk_gesture_single_set_touch_only (GTK_GESTURE_SINGLE (gesture), gestures_touch_only);

    return gesture;
}

StatusNotifierItem *status_notifier_item_new (const char *service)
{
    StatusNotifierItem *item = g_new0 (StatusNotifierItem, 1);
    char *name, *path;

    item->event_box = gtk_event_box_new ();
    item->icon = gtk_image_new ();
    gtk_container_add (GTK_CONTAINER (item->event_box), item->icon);

    name_and_obj_path (service, &name, &path);
    item->dbus_name = name;

    g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE, NULL,
        name, path, "org.kde.StatusNotifierItem", NULL, on_item_proxy_ready, item);

    g_free (path);

    item->gesture = detect_long_press (item->event_box);

    return item;
}

static void on_item_g_signal (GDBusProxy *proxy, const gchar *sender, const gchar *signal, GVariant *params, gpointer userdata)
{
    StatusNotifierItem *item = (StatusNotifierItem *) userdata;

    handle_signal (item, signal, params);
}

static void on_item_proxy_ready (GObject *source, GAsyncResult *res, gpointer userdata)
{
    StatusNotifierItem *item = (StatusNotifierItem *) userdata;

    item->item_proxy = g_dbus_proxy_new_for_bus_finish (res, NULL);
    if (!item->item_proxy) return;

    g_signal_connect (item->item_proxy, "g-signal", G_CALLBACK (on_item_g_signal), item);
    init_widget (item);
}

static GVariant *get_item_property_variant (StatusNotifierItem *item, const char *name)
{
    return g_dbus_proxy_get_cached_property (item->item_proxy, name);
}

static gboolean get_item_property_bool (StatusNotifierItem *item, const char *name, gboolean default_value)
{
    GVariant *v = get_item_property_variant (item, name);
    gboolean result = default_value;

    if (v)
    {
        if (g_variant_is_of_type (v, G_VARIANT_TYPE_BOOLEAN)) result = g_variant_get_boolean (v);
        g_variant_unref (v);
    }
    return result;
}

static char *get_item_property_string (StatusNotifierItem *item, const char *name)
{
    GVariant *v = get_item_property_variant (item, name);
    char *result = g_strdup ("");

    if (v)
    {
        if (g_variant_is_of_type (v, G_VARIANT_TYPE_STRING))
        {
            g_free (result);
            result = g_strdup (g_variant_get_string (v, NULL));
        }
        g_variant_unref (v);
    }
    return result;
}

static gboolean on_button_press_event (GtkWidget *widget, GdkEventButton *ev, gpointer userdata)
{
    StatusNotifierItem *item = (StatusNotifierItem *) userdata;

    if (item->menu && gtk_widget_is_visible (item->menu))
        pressed = PRESS_NONE;
    else
        pressed = PRESS_SHORT;
    return TRUE;
}

static gboolean on_button_release_event (GtkWidget *widget, GdkEventButton *ev, gpointer userdata)
{
    StatusNotifierItem *item = (StatusNotifierItem *) userdata;
    guint menu_btn = item->menu_on_middle_click ? GDK_BUTTON_MIDDLE : GDK_BUTTON_SECONDARY;
    guint secondary_activate_btn = item->menu_on_middle_click ? GDK_BUTTON_SECONDARY : GDK_BUTTON_MIDDLE;

    if (get_item_property_bool (item, "ItemIsMenu", TRUE) || (ev->button == menu_btn) || pressed == PRESS_LONG)
    {
        if (item->menu)
        {
            if (pressed != PRESS_NONE)
            {
                if (check_menu (item->menu))
                    show_menu_with_kbd_at_xy (item->event_box, item->menu, ev);
            }
        }
        else
        {
            g_dbus_proxy_call (item->item_proxy, "ContextMenu", g_variant_new ("(ii)", (int) ev->x, (int) ev->y),
                G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
        }
    }
    else if (ev->button == GDK_BUTTON_PRIMARY)
    {
        g_dbus_proxy_call (item->item_proxy, "Activate", g_variant_new ("(ii)", (int) ev->x, (int) ev->y),
            G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
    }
    else if (ev->button == secondary_activate_btn)
    {
        g_dbus_proxy_call (item->item_proxy, "SecondaryActivate", g_variant_new ("(ii)", (int) ev->x, (int) ev->y),
            G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
    }
    pressed = PRESS_NONE;

    return TRUE;
}

static gboolean on_scroll_event (GtkWidget *widget, GdkEventScroll *ev, gpointer userdata)
{
    StatusNotifierItem *item = (StatusNotifierItem *) userdata;
    int dx = 0;
    int dy = 0;

    switch (ev->direction)
    {
      case GDK_SCROLL_UP:
        dy = -1;
        break;

      case GDK_SCROLL_DOWN:
        dy = 1;
        break;

      case GDK_SCROLL_LEFT:
        dx = -1;
        break;

      case GDK_SCROLL_RIGHT:
        dx = 1;
        break;

      case GDK_SCROLL_SMOOTH:
        item->distance_scrolled_x += ev->delta_x;
        item->distance_scrolled_y += ev->delta_y;
        if (fabs (item->distance_scrolled_x) >= item->smooth_scrolling_threshold)
        {
            dx = lround (item->distance_scrolled_x);
            item->distance_scrolled_x = 0;
        }

        if (fabs (item->distance_scrolled_y) >= item->smooth_scrolling_threshold)
        {
            dy = lround (item->distance_scrolled_y);
            item->distance_scrolled_y = 0;
        }

        break;
    }

    if (dx != 0)
    {
        g_dbus_proxy_call (item->item_proxy, "Scroll", g_variant_new ("(is)", dx, "hozirontal"),
            G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
    }

    if (dy != 0)
    {
        g_dbus_proxy_call (item->item_proxy, "Scroll", g_variant_new ("(is)", dy, "vertical"),
            G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
    }

    return TRUE;
}

static void init_widget (StatusNotifierItem *item)
{
    status_notifier_item_update_icon (item);
    setup_tooltip (item);
    init_menu (item);

    g_signal_connect_after (item->event_box, "button-press-event", G_CALLBACK (on_button_press_event), item);
    g_signal_connect_after (item->event_box, "button-release-event", G_CALLBACK (on_button_release_event), item);

    gtk_widget_add_events (item->event_box, GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);

    g_signal_connect_after (item->event_box, "scroll-event", G_CALLBACK (on_scroll_event), item);
}

static gboolean on_query_tooltip (GtkWidget *widget, gint x, gint y, gboolean keyboard_mode, GtkTooltip *tooltip, gpointer userdata)
{
    StatusNotifierItem *item = (StatusNotifierItem *) userdata;
    GVariant *tt = get_item_property_variant (item, "ToolTip");
    char *tooltip_icon_name = g_strdup ("");
    GVariant *tooltip_icon_data = NULL;
    char *tooltip_title = g_strdup ("");
    char *tooltip_text = g_strdup ("");
    char *tooltip_label_text;
    GdkPixbuf *pixbuf;
    gboolean icon_shown = TRUE;
    gboolean result;

    if (tt && g_variant_is_of_type (tt, G_VARIANT_TYPE ("(sa(iiay)ss)")))
    {
        const gchar *icon_name, *title, *text;
        GVariant *icon_data;

        g_variant_get (tt, "(&s@a(iiay)&s&s)", &icon_name, &icon_data, &title, &text);
        g_free (tooltip_icon_name);
        tooltip_icon_name = g_strdup (icon_name);
        tooltip_icon_data = icon_data;
        g_free (tooltip_title);
        tooltip_title = g_strdup (title);
        g_free (tooltip_text);
        tooltip_text = g_strdup (text);
    }
    if (tt) g_variant_unref (tt);

    if (tooltip_text[0] && tooltip_title[0])
        tooltip_label_text = g_strdup_printf ("<b>%s</b>: %s", tooltip_title, tooltip_text);
    else if (tooltip_title[0])
        tooltip_label_text = g_strdup (tooltip_title);
    else if (tooltip_text[0])
        tooltip_label_text = g_strdup (tooltip_text);
    else
        tooltip_label_text = get_item_property_string (item, "Title");

    pixbuf = extract_pixbuf (tooltip_icon_data);
    if (tooltip_icon_data) g_variant_unref (tooltip_icon_data);

    if (gtk_icon_theme_has_icon (item->icon_theme, tooltip_icon_name))
    {
        gtk_tooltip_set_icon_from_icon_name (tooltip, tooltip_icon_name, GTK_ICON_SIZE_LARGE_TOOLBAR);
    }
    else if (pixbuf)
    {
        gtk_tooltip_set_icon (tooltip, pixbuf);
    }
    else
    {
        icon_shown = FALSE;
    }

    if (pixbuf) g_object_unref (pixbuf);

    gtk_tooltip_set_markup (tooltip, tooltip_label_text);
    result = icon_shown || tooltip_label_text[0];

    g_free (tooltip_icon_name);
    g_free (tooltip_title);
    g_free (tooltip_text);
    g_free (tooltip_label_text);

    return result;
}

static void setup_tooltip (StatusNotifierItem *item)
{
    gtk_widget_set_has_tooltip (item->event_box, TRUE);
    g_signal_connect_after (item->event_box, "query-tooltip", G_CALLBACK (on_query_tooltip), item);
}

void status_notifier_item_update_icon (StatusNotifierItem *item)
{
    char *icon_theme_path = get_item_property_string (item, "IconThemePath");
    char *status, *icon_type_name, *icon_name, *name_prop, *pixmap_prop;
    GVariant *pixmap_var;
    GdkPixbuf *pixmap_data;
    GtkIconInfo *icon_info;
    int size;

    if (icon_theme_path[0])
    {
        GtkIconTheme *new_theme = gtk_icon_theme_new ();

        gtk_icon_theme_add_resource_path (new_theme, icon_theme_path);
        if (item->icon_theme) g_object_unref (item->icon_theme);
        item->icon_theme = new_theme;
    }
    else
    {
        if (item->icon_theme) g_object_unref (item->icon_theme);
        item->icon_theme = (GtkIconTheme *) g_object_ref (gtk_icon_theme_get_default ());
    }
    g_free (icon_theme_path);

    status = get_item_property_string (item, "Status");
    icon_type_name = g_strdup (!g_strcmp0 (status, "NeedsAttention") ? "AttentionIcon" : "Icon");
    g_free (status);

    name_prop = g_strconcat (icon_type_name, "Name", NULL);
    icon_name = get_item_property_string (item, name_prop);
    g_free (name_prop);

    pixmap_prop = g_strconcat (icon_type_name, "Pixmap", NULL);
    pixmap_var = get_item_property_variant (item, pixmap_prop);
    g_free (pixmap_prop);
    pixmap_data = extract_pixbuf (pixmap_var);
    if (pixmap_var) g_variant_unref (pixmap_var);

    size = get_icon_size (item->icon);
    icon_info = gtk_icon_theme_lookup_icon (item->icon_theme, icon_name, size, (GtkIconLookupFlags) 0);
    if (icon_info)
    {
        set_taskbar_icon (item->icon, icon_name);
        g_object_unref (icon_info);
    }
    else if (pixmap_data)
    {
        int scale = gtk_widget_get_scale_factor (item->icon);
        GdkPixbuf *scaled = gdk_pixbuf_scale_simple (pixmap_data, scale * size, scale * size, GDK_INTERP_BILINEAR);

        gtk_image_set_from_pixbuf (GTK_IMAGE (item->icon), scaled);
        if (scaled) g_object_unref (scaled);
    }

    if (pixmap_data) g_object_unref (pixmap_data);

    g_free (icon_type_name);
    g_free (icon_name);
}

void status_notifier_item_set_params (StatusNotifierItem *item, gboolean momc, int sst)
{
    item->menu_on_middle_click = momc;
    item->smooth_scrolling_threshold = sst;
}

static void init_menu (StatusNotifierItem *item)
{
    GVariant *v = get_item_property_variant (item, "Menu");
    const char *menu_path = "";
    GtkWidget *raw_menu;

    if (v && g_variant_is_of_type (v, G_VARIANT_TYPE_OBJECT_PATH))
        menu_path = g_variant_get_string (v, NULL);

    if (!menu_path[0])
    {
        if (v) g_variant_unref (v);
        return;
    }

    raw_menu = GTK_WIDGET (dbusmenu_gtkmenu_new ((gchar *) item->dbus_name, (gchar *) menu_path));
    if (v) g_variant_unref (v);
    if (!raw_menu) return;

    item->menu = raw_menu;
    gtk_menu_attach_to_widget (GTK_MENU (item->menu), item->event_box, NULL);
}

typedef struct
{
    StatusNotifierItem *item;
    char *property_name;
    void (*callback) (gpointer);
    gpointer callback_data;
} FetchPropertyCtx;

static void on_fetch_property_done (GObject *source, GAsyncResult *res, gpointer userdata)
{
    FetchPropertyCtx *ctx = (FetchPropertyCtx *) userdata;
    GVariant *result;
    GError *error = NULL;

    result = g_dbus_proxy_call_finish (ctx->item->item_proxy, res, &error);
    if (result)
    {
        GVariant *value = NULL;

        g_variant_get (result, "(v)", &value);
        if (value)
        {
            g_dbus_proxy_set_cached_property (ctx->item->item_proxy, ctx->property_name, value);
            g_variant_unref (value);
        }
        g_variant_unref (result);
    }
    else g_clear_error (&error);

    if (ctx->callback) ctx->callback (ctx->callback_data);

    g_free (ctx->property_name);
    g_free (ctx);
}

static void fetch_property (StatusNotifierItem *item, const char *property_name, void (*callback) (gpointer), gpointer callback_data)
{
    FetchPropertyCtx *ctx = g_new0 (FetchPropertyCtx, 1);

    ctx->item = item;
    ctx->property_name = g_strdup (property_name);
    ctx->callback = callback;
    ctx->callback_data = callback_data;

    g_dbus_proxy_call (item->item_proxy, "org.freedesktop.DBus.Properties.Get",
        g_variant_new ("(ss)", "org.kde.StatusNotifierItem", property_name),
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, on_fetch_property_done, ctx);
}

static void on_fetch_update_icon (gpointer userdata)
{
    status_notifier_item_update_icon ((StatusNotifierItem *) userdata);
}

typedef struct
{
    StatusNotifierItem *item;
    char *pixmap_prop;
} IconFetchCtx;

static void icon_fetch_ctx_free (IconFetchCtx *ctx)
{
    g_free (ctx->pixmap_prop);
    g_free (ctx);
}

static void on_icon_pixmap_fetched (gpointer userdata)
{
    IconFetchCtx *ctx = (IconFetchCtx *) userdata;

    status_notifier_item_update_icon (ctx->item);
    icon_fetch_ctx_free (ctx);
}

static void on_icon_name_fetched (gpointer userdata)
{
    IconFetchCtx *ctx = (IconFetchCtx *) userdata;

    fetch_property (ctx->item, ctx->pixmap_prop, on_icon_pixmap_fetched, ctx);
}

static void handle_signal (StatusNotifierItem *item, const char *signal, GVariant *params)
{
    const char *property;
    size_t proplen;

    if (strlen (signal) < 3) return;

    property = signal + 3;
    proplen = strlen (property);

    if (!strcmp (property, "ToolTip"))
    {
        fetch_property (item, property, NULL, NULL);
    }
    else if (!strcmp (property, "IconThemePath"))
    {
        fetch_property (item, property, on_fetch_update_icon, item);
    }
    else if (proplen >= 4 && !strcmp (property + proplen - 4, "Icon"))
    {
        IconFetchCtx *ctx = g_new0 (IconFetchCtx, 1);
        char *name_prop;

        ctx->item = item;
        ctx->pixmap_prop = g_strconcat (property, "Pixmap", NULL);

        name_prop = g_strconcat (property, "Name", NULL);
        fetch_property (item, name_prop, on_icon_name_fetched, ctx);
        g_free (name_prop);
    }
    else if (!strcmp (property, "Status") && params && g_variant_is_of_type (params, G_VARIANT_TYPE ("(s)")))
    {
        const gchar *status;

        g_variant_get (params, "(&s)", &status);
        g_dbus_proxy_set_cached_property (item->item_proxy, property, g_variant_new_string (status));
        status_notifier_item_update_icon (item);
    }
}

void status_notifier_item_free (StatusNotifierItem *item)
{
    if (item->gesture) g_object_unref (item->gesture);
    if (item->item_proxy) g_object_unref (item->item_proxy);
    if (item->menu) gtk_widget_destroy (item->menu);
    if (item->icon_theme) g_object_unref (item->icon_theme);

    gtk_widget_destroy (item->event_box);

    g_free (item->dbus_name);

    g_free (item);
}

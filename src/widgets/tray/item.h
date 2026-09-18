#ifndef TRAY_ITEM_H
#define TRAY_ITEM_H

#include <gio/gio.h>
#include <gtk/gtk.h>

typedef struct _StatusNotifierItem StatusNotifierItem;

struct _StatusNotifierItem
{
    int smooth_scrolling_threshold;
    gboolean menu_on_middle_click;

    char *dbus_name;

    GDBusProxy *item_proxy;

    GtkWidget *event_box;
    GtkWidget *icon;
    GtkWidget *menu; /* NULL if absent */

    gdouble distance_scrolled_x;
    gdouble distance_scrolled_y;

    GtkIconTheme *icon_theme;

    GtkGesture *gesture;
};

extern StatusNotifierItem *status_notifier_item_new (const char *service);
extern void status_notifier_item_free (StatusNotifierItem *item);
extern void status_notifier_item_update_icon (StatusNotifierItem *item);
extern void status_notifier_item_set_params (StatusNotifierItem *item, gboolean momc, int sst);

#endif

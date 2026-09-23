#ifndef NETMAN_H
#define NETMAN_H

#include <gtk/gtk.h>
#include "plugin.h"

#define PLUGIN_TITLE N_("Network")

typedef struct {
    GtkWidget *plugin;
    GtkWidget *tray_icon;           /* Displayed image */
    GdkPixbuf *icons[7];            /* cached pixbufs: 0=offline, 1=ethernet, 2-6=wifi signal */
    GtkGesture *gesture;
    guint timer;
    GDBusConnection *dbus_conn;
    guint sub_id;
} NetmanPlugin;

extern conf_table_t conf_table[1];

extern void netman_init (NetmanPlugin *net);
extern void netman_update_display (NetmanPlugin *net);
extern void netman_destructor (gpointer user_data);

#endif

#ifndef TRAY_HOST_H
#define TRAY_HOST_H

#include "watcher.h"

#include <gio/gio.h>

typedef struct _WidgetStatusNotifier WidgetStatusNotifier;

typedef struct
{
    Watcher *watcher_ptr;

    guint dbus_name_id;

    guint watcher_id;
    GDBusProxy *watcher_proxy;

    WidgetStatusNotifier *tray;

    gboolean *destroyed;
} StatusNotifierHost;

extern StatusNotifierHost *status_notifier_host_new (WidgetStatusNotifier *tray);
extern void status_notifier_host_free (StatusNotifierHost *host);

#endif

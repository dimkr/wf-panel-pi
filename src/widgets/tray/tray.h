#ifndef TRAY_TRAY_H
#define TRAY_TRAY_H

#include <widget.h>

#include "item.h"
#include "host.h"

struct _WidgetStatusNotifier
{
    PanelWidget parent;

    StatusNotifierHost *host;

    GtkWidget *icons_hbox;
    GHashTable *items; /* char * (owned) -> StatusNotifierItem * (owned) */

    gboolean momc;
    int sst;
};

extern void widget_status_notifier_add_item (WidgetStatusNotifier *w, const char *service);
extern void widget_status_notifier_remove_item (WidgetStatusNotifier *w, const char *service);

#endif

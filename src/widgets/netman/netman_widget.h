#ifndef NETMAN_WIDGET_H
#define NETMAN_WIDGET_H

#include <gtk/gtk.h>
#include "widget.h"
#include "netman.h"

typedef struct
{
    PanelWidget parent;
    GtkWidget *plugin;
    NetmanPlugin *net;
} WidgetNetman;

extern PanelWidget *create (void);
extern void destroy (PanelWidget *w);
extern const conf_table_t *config_params (void);
extern const char *display_name (void);
extern const char *package_name (void);

#endif

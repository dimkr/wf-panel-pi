#ifndef WIDGET_H
#define WIDGET_H

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "plug_conf.h"

typedef struct _PanelWidget PanelWidget;

struct _PanelWidget
{
    char *widget_name;

    void (*widget_init) (PanelWidget *self, GtkWidget *container);
    void (*widget_free) (PanelWidget *self);
    void (*widget_command) (PanelWidget *self, const char *cmd);
    void (*widget_config_reload) (PanelWidget *self);
    void (*widget_set_icon) (PanelWidget *self);
};

#endif /* end of include guard: WIDGET_H */

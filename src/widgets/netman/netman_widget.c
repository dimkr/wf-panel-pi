#include "netman_widget.h"

static void widget_netman_set_icon (PanelWidget *self)
{
    WidgetNetman *w = (WidgetNetman *) self;

    netman_update_display (w->net);
}

static void widget_netman_config_reload (PanelWidget *self)
{
    load_configuration_data (PLUGIN_NAME, conf_table);
}

static void widget_netman_init (PanelWidget *self, GtkWidget *container)
{
    WidgetNetman *w = (WidgetNetman *) self;

    /* Create the button */
    w->plugin = gtk_button_new ();
    gtk_widget_set_name (w->plugin, PLUGIN_NAME);
    gtk_box_pack_start (GTK_BOX (container), w->plugin, FALSE, FALSE, 0);

    /* Setup structure */
    w->net = g_new0 (NetmanPlugin, 1);
    w->net->plugin = w->plugin;

    /* Initialise the plugin */
    load_configuration_data (PLUGIN_NAME, conf_table);
    netman_init (w->net);
}

static void widget_netman_free (PanelWidget *self)
{
    WidgetNetman *w = (WidgetNetman *) self;

    netman_destructor (w->net);
    gtk_widget_destroy (w->plugin);
}

PanelWidget *create (void)
{
    WidgetNetman *w = g_new0 (WidgetNetman, 1);

    w->parent.widget_init = widget_netman_init;
    w->parent.widget_free = widget_netman_free;
    w->parent.widget_set_icon = widget_netman_set_icon;
    w->parent.widget_config_reload = widget_netman_config_reload;

    return (PanelWidget *) w;
}

void destroy (PanelWidget *w)
{
    if (w->widget_free) w->widget_free (w);
    g_free (w);
}

const conf_table_t *config_params (void) { return conf_table; }
const char *display_name (void) { return PLUGIN_TITLE; }
const char *package_name (void) { return GETTEXT_PACKAGE; }

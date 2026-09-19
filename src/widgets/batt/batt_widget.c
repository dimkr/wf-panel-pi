#include "batt_widget.h"

static void widget_batt_set_icon (PanelWidget *self)
{
    WidgetBatt *w = (WidgetBatt *) self;

    batt_update_display (w->pt);
}

static void widget_batt_config_reload (PanelWidget *self)
{
    WidgetBatt *w = (WidgetBatt *) self;

    if (load_configuration_data (PLUGIN_NAME, conf_table)) batt_set_num (w->pt);
}

static void widget_batt_init (PanelWidget *self, GtkWidget *container)
{
    WidgetBatt *w = (WidgetBatt *) self;

    /* Create the button */
    w->plugin = gtk_button_new ();
    gtk_widget_set_name (w->plugin, PLUGIN_NAME);
    gtk_box_pack_start (GTK_BOX (container), w->plugin, FALSE, FALSE, 0);

    /* Setup structure */
    w->pt = g_new0 (PtBattPlugin, 1);
    w->pt->plugin = w->plugin;

    /* Initialise the plugin */
    batt_set_values (w->pt);
    load_configuration_data (PLUGIN_NAME, conf_table);
    batt_init (w->pt);
}

static void widget_batt_free (PanelWidget *self)
{
    WidgetBatt *w = (WidgetBatt *) self;

    batt_destructor (w->pt);
    gtk_widget_destroy (w->plugin);
}

PanelWidget *create (void)
{
    WidgetBatt *w = g_new0 (WidgetBatt, 1);

    w->parent.widget_init = widget_batt_init;
    w->parent.widget_free = widget_batt_free;
    w->parent.widget_set_icon = widget_batt_set_icon;
    w->parent.widget_config_reload = widget_batt_config_reload;

    return (PanelWidget *) w;
}

void destroy (PanelWidget *w)
{
    if (w->widget_free) w->widget_free (w);
    g_free (w);
}

const conf_table_t *config_params (void) { return conf_table; };
const char *display_name (void) { return PLUGIN_TITLE; };
const char *package_name (void) { return GETTEXT_PACKAGE; };

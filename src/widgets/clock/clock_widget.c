#include "clock_widget.h"

static void widget_clock_set_icon (PanelWidget *self)
{
    WidgetClock *w = (WidgetClock *) self;

    clock_update_display (w->clk);
}

static void widget_clock_config_reload (PanelWidget *self)
{
    WidgetClock *w = (WidgetClock *) self;

    if (load_configuration_data (PLUGIN_NAME, conf_table)) clock_update_display (w->clk);
}

static void widget_clock_init (PanelWidget *self, GtkWidget *container)
{
    WidgetClock *w = (WidgetClock *) self;

    /* Create the button */
    w->plugin = gtk_button_new ();
    gtk_widget_set_name (w->plugin, PLUGIN_NAME);
    gtk_box_pack_start (GTK_BOX (container), w->plugin, FALSE, FALSE, 0);

    /* Setup structure */
    w->clk = g_new0 (ClockPlugin, 1);
    w->clk->plugin = w->plugin;

    /* Initialise the plugin */
    clock_set_values (w->clk);
    load_configuration_data (PLUGIN_NAME, conf_table);
    clock_init (w->clk);
}

static void widget_clock_free (PanelWidget *self)
{
    WidgetClock *w = (WidgetClock *) self;

    clock_destructor (w->clk);
    gtk_widget_destroy (w->plugin);
}

PanelWidget *create (void)
{
    WidgetClock *w = g_new0 (WidgetClock, 1);

    w->parent.widget_init = widget_clock_init;
    w->parent.widget_free = widget_clock_free;
    w->parent.widget_set_icon = widget_clock_set_icon;
    w->parent.widget_config_reload = widget_clock_config_reload;

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

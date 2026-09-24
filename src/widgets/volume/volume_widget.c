#include "volume_widget.h"

static void widget_volume_command (PanelWidget *self, const char *cmd)
{
    WidgetVolume *w = (WidgetVolume *) self;

    volume_control_msg (w->vol, cmd);
}

static void widget_volume_set_icon (PanelWidget *self)
{
    WidgetVolume *w = (WidgetVolume *) self;

    volume_update_display (w->vol);
}

static void widget_volume_init (PanelWidget *self, GtkWidget *container)
{
    WidgetVolume *w = (WidgetVolume *) self;

    /* Create the button */
    w->plugin = gtk_box_new (panel_is_vertical (container) ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_name (w->plugin, PLUGIN_NAME);
    gtk_box_pack_start (GTK_BOX (container), w->plugin, FALSE, FALSE, 0);

    /* Setup structure */
    w->vol = g_new0 (VolumePlugin, 1);
    w->vol->plugin = w->plugin;

    /* Initialise the plugin */
    volume_init (w->vol);
}

static void widget_volume_free (PanelWidget *self)
{
    WidgetVolume *w = (WidgetVolume *) self;

    volume_destructor (w->vol);
    gtk_widget_destroy (w->plugin);
}

PanelWidget *create (void)
{
    WidgetVolume *w = g_new0 (WidgetVolume, 1);

    w->parent.widget_init = widget_volume_init;
    w->parent.widget_free = widget_volume_free;
    w->parent.widget_command = widget_volume_command;
    w->parent.widget_set_icon = widget_volume_set_icon;

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

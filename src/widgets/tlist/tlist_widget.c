#include "tlist_widget.h"

static void widget_tlist_command (PanelWidget *self, const char *cmd)
{
    WidgetWinlist *w = (WidgetWinlist *) self;

    wlist_control_msg (w->wl, cmd);
}

static void widget_tlist_set_icon (PanelWidget *self)
{
    WidgetWinlist *w = (WidgetWinlist *) self;

    wlist_update_display (w->wl);
}

static void widget_tlist_config_reload (PanelWidget *self)
{
    WidgetWinlist *w = (WidgetWinlist *) self;
    gboolean changed = load_configuration_data (PLUGIN_NAME, conf_table);

    char *ostr = g_strdup (w->wl->launchers);
    g_free (w->wl->launchers);
    get_config_string ("panel", "launchers", &w->wl->launchers, "");
    if (g_strcmp0 (w->wl->launchers, ostr)) changed = TRUE;
    g_free (ostr);

    if (changed) wlist_update_display (w->wl);
}

static void widget_tlist_init (PanelWidget *self, GtkWidget *container)
{
    WidgetWinlist *w = (WidgetWinlist *) self;

    /* Create the button */
    w->plugin = gtk_scrolled_window_new (NULL, NULL);
    gtk_widget_set_name (w->plugin, PLUGIN_NAME);
    if (panel_is_vertical (container))
    {
        gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (w->plugin), TRUE);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (w->plugin), GTK_POLICY_NEVER, GTK_POLICY_EXTERNAL);
    }
    else
    {
        gtk_scrolled_window_set_propagate_natural_width (GTK_SCROLLED_WINDOW (w->plugin), TRUE);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (w->plugin), GTK_POLICY_EXTERNAL, GTK_POLICY_NEVER);
    }

    gtk_box_pack_start (GTK_BOX (container), w->plugin, FALSE, FALSE, 0);

    /* Setup structure */
    w->wl = g_new0 (WinlistPlugin, 1);
    w->wl->plugin = w->plugin;

    /* Initialise the plugin */
    wlist_set_values (w->wl);
    load_configuration_data (PLUGIN_NAME, conf_table);
    get_config_string ("panel", "launchers", &w->wl->launchers, "");
    wlist_init (w->wl);
}

static void widget_tlist_free (PanelWidget *self)
{
    WidgetWinlist *w = (WidgetWinlist *) self;

    wlist_destructor (w->wl);
    gtk_widget_destroy (w->plugin);
}

PanelWidget *create (void)
{
    WidgetWinlist *w = g_new0 (WidgetWinlist, 1);

    w->parent.widget_init = widget_tlist_init;
    w->parent.widget_free = widget_tlist_free;
    w->parent.widget_command = widget_tlist_command;
    w->parent.widget_set_icon = widget_tlist_set_icon;
    w->parent.widget_config_reload = widget_tlist_config_reload;

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

#include "launchers_widget.h"

static void widget_launcher_command (PanelWidget *self, const char *cmd)
{
    WidgetLauncher *w = (WidgetLauncher *) self;

    launcher_control_msg (w->lch, cmd);
}

static void widget_launcher_set_icon (PanelWidget *self)
{
    WidgetLauncher *w = (WidgetLauncher *) self;

    launcher_update_display (w->lch);
}

static void widget_launcher_config_reload (PanelWidget *self)
{
    WidgetLauncher *w = (WidgetLauncher *) self;
    gboolean changed = load_configuration_data (PLUGIN_NAME, conf_table);

    char *ostr = g_strdup (w->lch->launchers);
    g_free (w->lch->launchers);
    get_config_string ("panel", "launchers", &w->lch->launchers, "");
    if (g_strcmp0 (w->lch->launchers, ostr)) changed = TRUE;
    g_free (ostr);

    if (changed) launcher_update_display (w->lch);
}

static void widget_launcher_init (PanelWidget *self, GtkWidget *container)
{
    WidgetLauncher *w = (WidgetLauncher *) self;

    /* Create the button */
    w->plugin = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_name (w->plugin, PLUGIN_NAME);
    gtk_box_pack_start (GTK_BOX (container), w->plugin, FALSE, FALSE, 0);

    /* Setup structure */
    w->lch = g_new0 (LauncherPlugin, 1);
    w->lch->plugin = w->plugin;

    /* Initialise the plugin */
    launcher_set_values (w->lch);
    load_configuration_data (PLUGIN_NAME, conf_table);
    get_config_string ("panel", "launchers", &w->lch->launchers, "");
    launcher_init (w->lch);
}

static void widget_launcher_free (PanelWidget *self)
{
    WidgetLauncher *w = (WidgetLauncher *) self;

    launcher_destructor (w->lch);
    gtk_widget_destroy (w->plugin);
}

PanelWidget *create (void)
{
    WidgetLauncher *w = g_new0 (WidgetLauncher, 1);

    w->parent.widget_init = widget_launcher_init;
    w->parent.widget_free = widget_launcher_free;
    w->parent.widget_command = widget_launcher_command;
    w->parent.widget_set_icon = widget_launcher_set_icon;
    w->parent.widget_config_reload = widget_launcher_config_reload;

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

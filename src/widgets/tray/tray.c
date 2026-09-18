#include "tray.h"

#include "plugin.h"

static conf_table_t conf_table[3] = {
    {CONF_TYPE_INT,     "smooth_scrolling_threshold",   N_("Smooth scrolling threshold"),   NULL,   "5"     },
    {CONF_TYPE_BOOL,    "menu_on_middle_click",         N_("Middle button activates menu"), NULL,   "false" },
    {CONF_TYPE_NONE,    NULL,                           NULL,                               NULL,   NULL    }
};

/*----------------------------------------------------------------------------*/
/* Prototypes                                                                 */
/*----------------------------------------------------------------------------*/

static void widget_tray_init (PanelWidget *self, GtkWidget *container);
static void widget_tray_free (PanelWidget *self);
static void widget_tray_set_icon (PanelWidget *self);
static void widget_tray_config_reload (PanelWidget *self);

void widget_status_notifier_add_item (WidgetStatusNotifier *w, const char *service)
{
    StatusNotifierItem *item;
    GHashTableIter iter;
    gpointer key, value;

    if (g_hash_table_contains (w->items, service)) return;

    item = status_notifier_item_new (service);
    g_hash_table_insert (w->items, g_strdup (service), item);
    gtk_box_pack_start (GTK_BOX (w->icons_hbox), item->event_box, TRUE, TRUE, 0);
    gtk_widget_show_all (w->icons_hbox);

    /* there's probably a better way of doing this... */
    g_hash_table_iter_init (&iter, w->items);
    while (g_hash_table_iter_next (&iter, &key, &value))
        status_notifier_item_set_params ((StatusNotifierItem *) value, w->momc, w->sst);
}

void widget_status_notifier_remove_item (WidgetStatusNotifier *w, const char *service)
{
    g_hash_table_remove (w->items, service);
    if (!g_hash_table_contains (w->items, service)) gtk_widget_hide (w->icons_hbox);
}

static void widget_tray_set_icon (PanelWidget *self)
{
    WidgetStatusNotifier *w = (WidgetStatusNotifier *) self;
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init (&iter, w->items);
    while (g_hash_table_iter_next (&iter, &key, &value))
        status_notifier_item_update_icon ((StatusNotifierItem *) value);
}

static void widget_tray_config_reload (PanelWidget *self)
{
    WidgetStatusNotifier *w = (WidgetStatusNotifier *) self;

    if (load_configuration_data (PLUGIN_NAME, conf_table))
    {
        GHashTableIter iter;
        gpointer key, value;

        g_hash_table_iter_init (&iter, w->items);
        while (g_hash_table_iter_next (&iter, &key, &value))
            status_notifier_item_set_params ((StatusNotifierItem *) value, w->momc, w->sst);
    }
}

static void widget_tray_init (PanelWidget *self, GtkWidget *container)
{
    WidgetStatusNotifier *w = (WidgetStatusNotifier *) self;

    gtk_widget_set_name (w->icons_hbox, PLUGIN_NAME);
    gtk_box_set_spacing (GTK_BOX (w->icons_hbox), 5);
    gtk_container_add (GTK_CONTAINER (container), w->icons_hbox);

    conf_table[0].value = (void **) &w->sst;
    conf_table[1].value = (void **) &w->momc;

    load_configuration_data (PLUGIN_NAME, conf_table);
}

static void widget_tray_free (PanelWidget *self)
{
    WidgetStatusNotifier *w = (WidgetStatusNotifier *) self;

    status_notifier_host_free (w->host);
    g_hash_table_destroy (w->items);
    gtk_widget_destroy (w->icons_hbox);
}

PanelWidget *create (void)
{
    WidgetStatusNotifier *w = g_new0 (WidgetStatusNotifier, 1);

    w->icons_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    w->items = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) status_notifier_item_free);

    w->host = status_notifier_host_new (w);

    w->parent.widget_init = widget_tray_init;
    w->parent.widget_free = widget_tray_free;
    w->parent.widget_set_icon = widget_tray_set_icon;
    w->parent.widget_config_reload = widget_tray_config_reload;

    return (PanelWidget *) w;
}

void destroy (PanelWidget *w)
{
    if (w->widget_free) w->widget_free (w);
    g_free (w);
}

const conf_table_t *config_params (void) { return conf_table; };
const char *display_name (void) { return N_("System Tray"); };
const char *package_name (void) { return GETTEXT_PACKAGE; };

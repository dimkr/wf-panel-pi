#include <widget.h>

typedef struct
{
    PanelWidget parent;

    GtkWidget *box;
} WidgetSplit;

static void widget_split_init (PanelWidget *self, GtkWidget *container)
{
    WidgetSplit *w = (WidgetSplit *) self;

    gtk_widget_set_name (w->box, "split");
    gtk_box_pack_start (GTK_BOX (container), w->box, FALSE, FALSE, 0);
    gtk_widget_show_all (w->box);
}

static void widget_split_free (PanelWidget *self)
{
    WidgetSplit *w = (WidgetSplit *) self;

    gtk_widget_destroy (w->box);
}

static const conf_table_t conf_table[1] = {
    {CONF_TYPE_NONE,    NULL,   NULL,   NULL,   NULL }
};

PanelWidget *create (void)
{
    WidgetSplit *w = g_new0 (WidgetSplit, 1);

    w->box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

    w->parent.widget_init = widget_split_init;
    w->parent.widget_free = widget_split_free;

    return (PanelWidget *) w;
}

void destroy (PanelWidget *w)
{
    if (w->widget_free) w->widget_free (w);
    g_free (w);
}

const conf_table_t *config_params (void) { return conf_table; };
const char *display_name (void) { return N_("Tray Split"); };
const char *package_name (void) { return GETTEXT_PACKAGE; };

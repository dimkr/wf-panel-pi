#include <widget.h>

typedef struct
{
    PanelWidget parent;

    GtkWidget *box;
} WidgetSpacing;

static void widget_spacing_init (PanelWidget *self, GtkWidget *container)
{
    WidgetSpacing *w = (WidgetSpacing *) self;

    gtk_widget_set_name (w->box, "spacing");
    gtk_box_pack_start (GTK_BOX (container), w->box, FALSE, FALSE, 0);
    gtk_widget_show_all (w->box);
}

static void widget_spacing_free (PanelWidget *self)
{
    WidgetSpacing *w = (WidgetSpacing *) self;

    gtk_widget_destroy (w->box);
}

static const conf_table_t conf_table[2] = {
    {CONF_TYPE_INT,     "width",    N_("Width in pixels"),  NULL,   "4"     },
    {CONF_TYPE_NONE,    NULL,       NULL,                   NULL,   NULL    }
};

PanelWidget *create (int val)
{
    WidgetSpacing *w = g_new0 (WidgetSpacing, 1);

    w->box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request (w->box, val, 1);

    w->parent.widget_init = widget_spacing_init;
    w->parent.widget_free = widget_spacing_free;

    return (PanelWidget *) w;
}

void destroy (PanelWidget *w)
{
    if (w->widget_free) w->widget_free (w);
    g_free (w);
}

const conf_table_t *config_params (void) { return conf_table; };
const char *display_name (void) { return N_("Spacer"); };
const char *package_name (void) { return GETTEXT_PACKAGE; };

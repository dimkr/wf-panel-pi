#include <widget.h>

typedef struct
{
    PanelWidget parent;

    GtkWidget *box;
    GtkWidget *da;
} WidgetSeparator;

static gboolean widget_separator_draw (GtkWidget *da, cairo_t *cr, gpointer data)
{
    WidgetSeparator *w = (WidgetSeparator *) data;
    GtkAllocation palloc, alloc;
    GtkWidget *widget;

    gtk_widget_get_allocation (w->box, &alloc);
    widget = w->box;
    while (widget)
    {
        gtk_widget_get_allocation (widget, &palloc);
        widget = gtk_widget_get_parent (widget);
    }

    if (alloc.x == 0 || alloc.x + 1 == palloc.width) return TRUE;

    GtkStyleContext *sc = gtk_widget_get_style_context (w->da);
    GdkRGBA fg;
    gtk_style_context_get_color (sc, gtk_style_context_get_state (sc), &fg);
    int height = gtk_widget_get_allocated_height (w->da);

    cairo_set_source_rgb (cr, fg.red, fg.green, fg.blue);
    cairo_rectangle (cr, 0, 0 + height >> 2, 1, height >> 1);
    cairo_fill (cr);

    return TRUE;
}

static void widget_separator_init (PanelWidget *self, GtkWidget *container)
{
    WidgetSeparator *w = (WidgetSeparator *) self;

    gtk_widget_set_name (w->box, "separator");
    gtk_box_pack_start (GTK_BOX (container), w->box, FALSE, FALSE, 0);
    gtk_widget_show_all (w->box);
}

static void widget_separator_free (PanelWidget *self)
{
    WidgetSeparator *w = (WidgetSeparator *) self;

    gtk_widget_destroy (w->box);
}

static const conf_table_t conf_table[1] = {
    {CONF_TYPE_NONE,    NULL,   NULL,   NULL,   NULL}
};

PanelWidget *create (void)
{
    WidgetSeparator *w = g_new0 (WidgetSeparator, 1);

    w->box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    w->da = gtk_drawing_area_new ();
    gtk_widget_set_size_request (w->box, 1, -1);
    gtk_box_pack_start (GTK_BOX (w->box), w->da, TRUE, TRUE, 0);

    g_signal_connect (w->da, "draw", G_CALLBACK (widget_separator_draw), w);

    w->parent.widget_init = widget_separator_init;
    w->parent.widget_free = widget_separator_free;

    return (PanelWidget *) w;
}

void destroy (PanelWidget *w)
{
    if (w->widget_free) w->widget_free (w);
    g_free (w);
}

const conf_table_t *config_params (void) { return conf_table; };
const char *display_name (void) { return N_("Separator"); };
const char *package_name (void) { return GETTEXT_PACKAGE; };

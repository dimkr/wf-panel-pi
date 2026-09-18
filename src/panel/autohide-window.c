/*============================================================================
Copyright (c) 2026 Raspberry Pi
All rights reserved.

Some code based on the wf-shell project copyright (c) 2018 Ilia Bozhinov

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
============================================================================*/

#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>

#include "plug_conf.h"
#include "autohide-window.h"

#define AUTOHIDE_HIDE_DELAY 500
#define MARGIN 5

extern GtkWindow *popwindow;

/*----------------------------------------------------------------------------*/
/* Prototypes                                                                 */
/*----------------------------------------------------------------------------*/

static GtkLayerShellEdge get_anchor_edge (AutohidingWindow *win);
static void increase_autohide (AutohidingWindow *win);
static void decrease_autohide (AutohidingWindow *win);
static gboolean should_autohide (AutohidingWindow *win);
static void start_animation (AutohidingWindow *win, int target);
static gboolean do_hide (gpointer userdata);
static gboolean do_show (gpointer userdata);
static void schedule_hide (AutohidingWindow *win, int delay);
static void schedule_show (AutohidingWindow *win, int delay);
static void update_margin (AutohidingWindow *win);
static void update_autohide (AutohidingWindow *win);
static void set_layer (AutohidingWindow *win);
static unsigned char load_config (AutohidingWindow *win);

static gboolean on_draw (GtkWidget *widget, cairo_t *cr, gpointer userdata);
static void on_size_allocate (GtkWidget *widget, GtkAllocation *alloc, gpointer userdata);
static gboolean on_enter_notify_event (GtkWidget *widget, GdkEventCrossing *ev, gpointer userdata);
static gboolean on_leave_notify_event (GtkWidget *widget, GdkEventCrossing *ev, gpointer userdata);

/* Public methods */

AutohidingWindow *autohide_window_new (gboolean is_dock)
{
    AutohidingWindow *win = g_new0 (AutohidingWindow, 1);

    win->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);

    win->dock = is_dock;

    win->tz = g_time_zone_new_local ();

    load_config (win);

    gtk_window_set_decorated (GTK_WINDOW (win->window), FALSE);
    gtk_window_set_resizable (GTK_WINDOW (win->window), FALSE);

    gtk_layer_init_for_window (GTK_WINDOW (win->window));
    gtk_layer_set_namespace (GTK_WINDOW (win->window), "$unfocus panel");
    gtk_layer_set_keyboard_mode (GTK_WINDOW (win->window), GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);

    g_object_set (gtk_widget_get_settings (win->window), "gtk-visible-focus", GTK_POLICY_AUTOMATIC, NULL);

    win->last_autohide_value = win->autohide;
    win->autohide_counter = (int) win->autohide;

    autohide_window_set_monitor (win);
    autohide_window_set_auto_exclusive_zone (win, !win->autohide);
    autohide_window_update_position (win);
    set_layer (win);

    g_signal_connect (win->window, "draw", G_CALLBACK (on_draw), win);
    g_signal_connect (win->window, "size-allocate", G_CALLBACK (on_size_allocate), win);
    g_signal_connect (win->window, "enter-notify-event", G_CALLBACK (on_enter_notify_event), win);
    g_signal_connect (win->window, "leave-notify-event", G_CALLBACK (on_leave_notify_event), win);

    return win;
}

void autohide_window_free (AutohidingWindow *win)
{
    g_time_zone_unref (win->tz);

    g_free (win->position);
    g_free (win->layer);
    g_free (win->monitor);

    if (win->mon) g_object_unref (win->mon);

    gtk_widget_destroy (win->window);
}

void autohide_window_set_auto_exclusive_zone (AutohidingWindow *win, gboolean has_zone)
{
    int target_zone = has_zone ? gtk_widget_get_allocated_height (win->window) : 0;
    win->has_auto_exclusive_zone = has_zone;

    if (win->last_zone != target_zone)
    {
        gtk_layer_set_exclusive_zone (GTK_WINDOW (win->window), target_zone);
        win->last_zone = target_zone;
    }
}

void autohide_window_set_monitor (AutohidingWindow *win)
{
    GdkDisplay *disp = gdk_display_get_default ();
    GdkScreen *screen = gdk_display_get_default_screen (disp);
    int try_mon;
    const char *mnumstr = win->monitor;
    char *mname;
    GdkMonitor *new_mon;

    if (strlen (mnumstr) == 1 && sscanf (mnumstr, "%d", &try_mon) == 1)
    {
        // single digit - interpret as monitor number
        while (try_mon >= 0)
        {
            new_mon = gdk_display_get_monitor (disp, try_mon);
            if (win->mon) g_object_unref (win->mon);
            win->mon = new_mon;
            if (win->mon) g_object_ref (win->mon);
            if (win->mon) break;
            try_mon--;
        }
    }
    else
    {
        // output name - try to match it to a connected monitor
        for (try_mon = gdk_display_get_n_monitors (disp) - 1; try_mon >= 0; try_mon--)
        {
            new_mon = gdk_display_get_monitor (disp, try_mon);
            if (win->mon) g_object_unref (win->mon);
            win->mon = new_mon;
            if (win->mon) g_object_ref (win->mon);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
            mname = gdk_screen_get_monitor_plug_name (screen, try_mon);
#pragma GCC diagnostic pop
            if (!g_strcmp0 (mname, mnumstr) && win->mon)
            {
                g_free (mname);
                break;
            }
            g_free (mname);
        }
    }

    if (win->mon) gtk_layer_set_monitor (GTK_WINDOW (win->window), win->mon);
}

/* Private methods */

static GtkLayerShellEdge get_anchor_edge (AutohidingWindow *win)
{
    if (!g_strcmp0 (win->position, "bottom")) return GTK_LAYER_SHELL_EDGE_BOTTOM;
    return GTK_LAYER_SHELL_EDGE_TOP;
}

static void increase_autohide (AutohidingWindow *win)
{
    win->autohide_counter++;
    if (should_autohide (win)) schedule_hide (win, 0);
}

static void decrease_autohide (AutohidingWindow *win)
{
    win->autohide_counter--;
    if (win->autohide_counter < 0) win->autohide_counter = 0;
    if (!should_autohide (win)) schedule_show (win, 0);
}

static gboolean should_autohide (AutohidingWindow *win)
{
    return win->autohide_counter && !win->input_inside_panel;
}

static void start_animation (AutohidingWindow *win, int target)
{
    win->start_marg = gtk_layer_get_margin (GTK_WINDOW (win->window), get_anchor_edge (win));
    win->targ_marg = target;
    win->anim_start = g_date_time_new_now (win->tz);
}

static gboolean do_hide (gpointer userdata)
{
    AutohidingWindow *win = (AutohidingWindow *) userdata;

    win->pending_hide = 0;
    start_animation (win, win->remainder - gtk_widget_get_allocated_height (win->window));
    update_margin (win);
    return G_SOURCE_REMOVE;
}

static gboolean do_show (gpointer userdata)
{
    AutohidingWindow *win = (AutohidingWindow *) userdata;

    win->pending_show = 0;
    start_animation (win, win->offset);
    update_margin (win);
    return G_SOURCE_REMOVE;
}

static void schedule_hide (AutohidingWindow *win, int delay)
{
    if (win->pending_show)
    {
        g_source_remove (win->pending_show);
        win->pending_show = 0;
    }
    if (delay == 0) do_hide (win);
    else if (!win->pending_hide)
    {
        win->pending_hide = g_timeout_add (delay, do_hide, win);
    }
}

static void schedule_show (AutohidingWindow *win, int delay)
{
    if (win->pending_hide)
    {
        g_source_remove (win->pending_hide);
        win->pending_hide = 0;
    }
    if (delay == 0) do_show (win);
    else if (!win->pending_show)
    {
        win->pending_show = g_timeout_add (delay, do_show, win);
    }
}

void autohide_window_update_position (AutohidingWindow *win)
{
    /* Reset old anchors */
    gtk_layer_set_anchor (GTK_WINDOW (win->window), GTK_LAYER_SHELL_EDGE_TOP, FALSE);
    gtk_layer_set_anchor (GTK_WINDOW (win->window), GTK_LAYER_SHELL_EDGE_BOTTOM, FALSE);

    /* Set new anchor */
    gtk_layer_set_anchor (GTK_WINDOW (win->window), get_anchor_edge (win), TRUE);

    /* When the position changes, show an animation from the new edge. */
    start_animation (win, -gtk_widget_get_allocated_height (win->window));

    /* Show the window */
    schedule_show (win, 0);

    /* Hide the window afterwards if autohide is enabled */
    if (should_autohide (win)) schedule_hide (win, AUTOHIDE_HIDE_DELAY);
}

static void update_margin (AutohidingWindow *win)
{
    int pos = gtk_layer_get_margin (GTK_WINDOW (win->window), get_anchor_edge (win));
    if (pos != win->targ_marg)
    {
        GDateTime *now = g_date_time_new_now (win->tz);
        int elapsed = g_date_time_difference (now, win->anim_start) / 1000;
        g_date_time_unref (now);
        if (elapsed > win->duration)
        {
            pos = win->targ_marg;
            g_date_time_unref (win->anim_start);
        }
        else
        {
            pos = (win->targ_marg - win->start_marg) * elapsed;
            pos /= win->duration;
            pos += win->start_marg;
        }
        gtk_layer_set_margin (GTK_WINDOW (win->window), get_anchor_edge (win), pos);
        gtk_widget_queue_draw (win->window);
    }
}

static void update_autohide (AutohidingWindow *win)
{
    if (win->autohide == win->last_autohide_value) return;

    if (win->autohide) increase_autohide (win);
    else decrease_autohide (win);

    win->last_autohide_value = win->autohide;
    autohide_window_set_auto_exclusive_zone (win, !win->autohide);
    set_layer (win);
}

static void set_layer (AutohidingWindow *win)
{
    if (win->autohide)
    {
        // if autohide is enabled, keep on top
        gtk_layer_set_layer (GTK_WINDOW (win->window), GTK_LAYER_SHELL_LAYER_TOP);
        return;
    }

    if (!g_strcmp0 (win->layer, "overlay")) gtk_layer_set_layer (GTK_WINDOW (win->window), GTK_LAYER_SHELL_LAYER_OVERLAY);
    if (!g_strcmp0 (win->layer, "top")) gtk_layer_set_layer (GTK_WINDOW (win->window), GTK_LAYER_SHELL_LAYER_TOP);
    if (!g_strcmp0 (win->layer, "bottom")) gtk_layer_set_layer (GTK_WINDOW (win->window), GTK_LAYER_SHELL_LAYER_BOTTOM);
    if (!g_strcmp0 (win->layer, "background")) gtk_layer_set_layer (GTK_WINDOW (win->window), GTK_LAYER_SHELL_LAYER_BACKGROUND);
}

#define CFG_POSITION    0x01
#define CFG_MONITOR     0x02
#define CFG_LAYER       0x04
#define CFG_AHIDE       0x08

static unsigned char load_config (AutohidingWindow *win)
{
    unsigned char changes = 0;
    char *tmp;
    int val;

    get_config_string (win->dock ? "dock" : "panel", "position", &tmp, win->dock ? "bottom" : "top");
    if (g_strcmp0 (tmp, win->position))
    {
        g_free (win->position);
        win->position = tmp;
        changes |= CFG_POSITION;
    }
    else g_free (tmp);

    get_config_string (win->dock ? "dock" : "panel", "layer", &tmp, win->dock ? "top" : "bottom");
    if (g_strcmp0 (tmp, win->layer))
    {
        g_free (win->layer);
        win->layer = tmp;
        changes |= CFG_LAYER;
    }
    else g_free (tmp);

    get_config_string (win->dock ? "dock" : "panel", "monitor", &tmp, "0");
    if (g_strcmp0 (tmp, win->monitor))
    {
        g_free (win->monitor);
        win->monitor = tmp;
        changes |= CFG_MONITOR;
    }
    else g_free (tmp);

    val = get_config_int (win->dock ? "dock" : "panel", "offset", win->dock ? "5" : "0");
    if (win->offset != val)
    {
        win->offset = val;
        changes |= CFG_POSITION;
    }

    val = get_config_int (win->dock ? "dock" : "panel", "remainder", "5");
    if (win->remainder != val)
    {
        win->remainder = val;
        changes |= CFG_POSITION;
    }

    val = get_config_bool (win->dock ? "dock" : "panel", "autohide", "false");
    if (win->autohide != val)
    {
        win->autohide = val;
        changes |= CFG_AHIDE;
    }

    win->duration = get_config_int ("panel", "autohide_duration", "300");

    return changes;
}

void autohide_window_handle_config_reload (AutohidingWindow *win)
{
    unsigned char changes = load_config (win);
    if (changes & CFG_LAYER) set_layer (win);
    if (changes & CFG_MONITOR) autohide_window_set_monitor (win);
    if (changes & CFG_POSITION) autohide_window_update_position (win);
    if (changes & CFG_AHIDE) update_autohide (win);
}

static gboolean on_draw (GtkWidget *widget, cairo_t *cr, gpointer userdata)
{
    AutohidingWindow *win = (AutohidingWindow *) userdata;

    update_margin (win);

    return FALSE;
}

static void on_size_allocate (GtkWidget *widget, GtkAllocation *alloc, gpointer userdata)
{
    AutohidingWindow *win = (AutohidingWindow *) userdata;

    autohide_window_set_auto_exclusive_zone (win, win->has_auto_exclusive_zone);
}

static gboolean on_enter_notify_event (GtkWidget *widget, GdkEventCrossing *ev, gpointer userdata)
{
    AutohidingWindow *win = (AutohidingWindow *) userdata;

    if (!win->autohide) return FALSE;
    if (win->pending_hide)
    {
        g_source_remove (win->pending_hide);
        win->pending_hide = 0;
    }
    win->input_inside_panel = TRUE;

    /*
     * If the button which opened a menu is clicked again to close it, an
     * enter event is generated in which the root coords are the values of the
     * standard coords rounded down to the nearest int. This condition is
     * detected as a special case and used to ignore the next program-generated
     * leave event, which would otherwise cause an autohide.
     */
    if ((int) ev->x == ev->x_root && (int) ev->y == ev->y_root) win->noleave = TRUE;

    schedule_show (win, 0);

    return FALSE;
}

static gboolean on_leave_notify_event (GtkWidget *widget, GdkEventCrossing *ev, gpointer userdata)
{
    AutohidingWindow *win = (AutohidingWindow *) userdata;

    if (!win->autohide) return FALSE;
    if (ev->detail == GDK_NOTIFY_INFERIOR) return FALSE;

    // see explanation above!
    if (ev->x == 0.0 && ev->y == 0.0 && win->noleave == TRUE)
    {
        win->noleave = FALSE;
        return FALSE;
    }

    // don't hide if leaving a window towards the closest edge
    if (ev->x > MARGIN && ev->x < gtk_widget_get_allocated_width (win->window) - MARGIN)
    {
        if (get_anchor_edge (win) == GTK_LAYER_SHELL_EDGE_TOP)
        {
            if (ev->y < MARGIN) return FALSE;
        }
        else
        {
            if (ev->y > gtk_widget_get_allocated_height (win->window) - MARGIN) return FALSE;
        }
    }

    win->input_inside_panel = FALSE;
    if (should_autohide (win) && !popwindow) schedule_hide (win, AUTOHIDE_HIDE_DELAY);

    return FALSE;
}

/* End of file */
/*----------------------------------------------------------------------------*/

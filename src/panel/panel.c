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

#include <dlfcn.h>
#include <fnmatch.h>
#include <unistd.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <gtk-layer-shell.h>

#include "widget.h"

#include "plug_conf.h"
#include "plugin.h"

#include "panel.h"

/*----------------------------------------------------------------------------*/
/* Prototypes                                                                 */
/*----------------------------------------------------------------------------*/

static unsigned char load_config (Panel *panel);
static void set_exclusive (Panel *panel);
static gboolean on_keypress_event (GtkWidget *widget, GdkEventKey *event, gpointer userdata);
static gboolean on_button_press_event (GtkWidget *widget, GdkEventButton *event, gpointer userdata);
static gboolean on_button_release_event (GtkWidget *widget, GdkEventButton *event, gpointer userdata);
static gboolean on_delete (GtkWidget *widget, GdkEventAny *ev, gpointer userdata);
static void do_plugin_configure (GtkWidget *widget, gpointer userdata);
static void do_configure (GtkWidget *widget, gpointer userdata);
static void do_notify_configure (GtkWidget *widget, gpointer userdata);
static void do_appearance_set (GtkWidget *widget, gpointer userdata);
static PanelWidget *widget_from_name (const char *name);
static void free_panel_widget (PanelWidget *widget);
static void reload_widgets (const char *list, GList **container, GtkWidget *box);
static void init_widgets (Panel *panel);
static void init_notify (Panel *panel);
static void update_widget_icons (Panel *panel);

static gboolean on_draw_log_first (GtkWidget *widget, cairo_t *cr, gpointer userdata);
static gboolean on_draw_dock_layout (GtkWidget *widget, cairo_t *cr, gpointer userdata);
static void on_gesture_pressed (GtkGesture *gesture, gdouble x, gdouble y, gpointer userdata);
static void on_gesture_end (GtkGesture *gesture, GdkEventSequence *sequence, gpointer userdata);

Panel *panel_new (gboolean is_dock)
{
    Panel *panel = g_new0 (Panel, 1);
    const char *rpi_log_env;

    panel->dock = is_dock;

    // Load configuration files
    load_config (panel);

    // Check for running on a Pi
    if (!access ("/boot/firmware/config.txt", R_OK)) is_pi_var = TRUE;
    else is_pi_var = FALSE;

    // Check for running under wizard
    if (!g_strcmp0 (getenv ("USER"), "rpi-first-boot-wizard")) panel->wizard = TRUE;
    else panel->wizard = FALSE;

    // Create the window
    panel->window = autohide_window_new (panel->dock);

    // GTK settings for window
    gtk_widget_set_name (panel->window->window, panel->dock ? "DockToplevel" : "PanelToplevel");
    panel->grid = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name (panel->grid, "grid");

    // Set the icon size data pointer
    g_object_set_data (G_OBJECT (panel->window->window), "icon-size", &panel->icon_size);

    // Connect to draw signal to log first draw event using journald only if RPI_LOG_FIRST_DRAW is set
    rpi_log_env = getenv ("RPI_LOG_FIRST_DRAW");
    if (rpi_log_env && (strcmp (rpi_log_env, "1") == 0 || strcmp (rpi_log_env, "true") == 0 ||
        strcmp (rpi_log_env, "yes") == 0 || strcmp (rpi_log_env, "on") == 0))
    {
        panel->draw_connection = g_signal_connect_after (panel->window->window, "draw", G_CALLBACK (on_draw_log_first), panel);
    }

    // Monitor the draw signal to detect changes in scaling and reload icons if detected
    panel->pending_update = FALSE;
    panel->scaling = gtk_widget_get_scale_factor (panel->window->window);
    g_signal_connect_after (panel->window->window, "draw", G_CALLBACK (on_draw_dock_layout), panel);

    // Create window menu
    panel->menu = gtk_menu_new ();

    panel->cplug = gtk_menu_item_new_with_label (_("Configure Plugin..."));
    g_signal_connect (panel->cplug, "activate", G_CALLBACK (do_plugin_configure), panel);
    gtk_menu_attach (GTK_MENU (panel->menu), panel->cplug, 0, 1, 0, 1);

    panel->sep = gtk_separator_menu_item_new ();
    gtk_menu_attach (GTK_MENU (panel->menu), panel->sep, 0, 1, 1, 2);

    panel->conf = gtk_menu_item_new_with_label (_("Add / Remove Plugins..."));
    g_signal_connect (panel->conf, "activate", G_CALLBACK (do_configure), panel);
    gtk_menu_attach (GTK_MENU (panel->menu), panel->conf, 0, 1, 2, 3);

    panel->notif = gtk_menu_item_new_with_label (_("Notifications..."));
    g_signal_connect (panel->notif, "activate", G_CALLBACK (do_notify_configure), panel);
    gtk_menu_attach (GTK_MENU (panel->menu), panel->notif, 0, 1, 3, 4);

    panel->appset = gtk_menu_item_new_with_label (panel->dock ? _("Dock Preferences...") : _("Taskbar Preferences..."));
    g_signal_connect (panel->appset, "activate", G_CALLBACK (do_appearance_set), panel);
    gtk_menu_attach (GTK_MENU (panel->menu), panel->appset, 0, 1, 4, 5);

    gtk_menu_attach_to_widget (GTK_MENU (panel->menu), panel->window->window, NULL);
    gtk_widget_show_all (panel->menu);

    // Set up window event handlers
    g_signal_connect_after (panel->window->window, "button-press-event", G_CALLBACK (on_button_press_event), panel);
    g_signal_connect_after (panel->window->window, "button-release-event", G_CALLBACK (on_button_release_event), panel);
    g_signal_connect_after (panel->window->window, "key-press-event", G_CALLBACK (on_keypress_event), panel);
    g_signal_connect_after (panel->window->window, "delete-event", G_CALLBACK (on_delete), panel);

    // Set up long press handler
    panel->gesture = gtk_gesture_long_press_new (panel->window->window);
    gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (panel->gesture), GTK_PHASE_BUBBLE);
    g_signal_connect (panel->gesture, "pressed", G_CALLBACK (on_gesture_pressed), panel);
    g_signal_connect (panel->gesture, "end", G_CALLBACK (on_gesture_end), panel);
    gtk_gesture_single_set_touch_only (GTK_GESTURE_SINGLE (panel->gesture), gestures_touch_only);

    // Create the window
    panel->content_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    panel->left_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    panel->right_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    panel->right2_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start (GTK_BOX (panel->content_box), panel->left_box, FALSE, FALSE, 0);
    if (panel->dock)
    {
        gtk_box_pack_end (GTK_BOX (panel->content_box), panel->grid, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (panel->grid), panel->right_box, FALSE, FALSE, 0);
        gtk_box_pack_end (GTK_BOX (panel->grid), panel->right2_box, FALSE, FALSE, 0);
    }
    else gtk_box_pack_end (GTK_BOX (panel->content_box), panel->right_box, FALSE, FALSE, 0);
    gtk_container_add (GTK_CONTAINER (panel->window->window), panel->content_box);
    gtk_widget_show_all (panel->window->window);

    // Setup notifications
    init_notify (panel);

    // Load widgets
    reload = FALSE;
    init_widgets (panel);
    update_widget_icons (panel);

    return panel;
}

void panel_free (Panel *panel)
{
    GList *l;

    if (!panel->dock) wfpanel_notify_close ();

    for (l = panel->left_widgets; l; l = l->next) free_panel_widget ((PanelWidget *) l->data);
    g_list_free (panel->left_widgets);

    for (l = panel->right_widgets; l; l = l->next) free_panel_widget ((PanelWidget *) l->data);
    g_list_free (panel->right_widgets);

    if (panel->gesture) g_object_unref (panel->gesture);

    gtk_widget_destroy (panel->menu);

    autohide_window_free (panel->window);

    g_free (panel->left_widgets_opt);
    g_free (panel->right_widgets_opt);

    g_free (panel);
}

// Load panel configuration or use defaults

#define CFG_WIDGETS 0x01
#define CFG_NOTIFY  0x02
#define CFG_ICONS   0x04
#define CFG_EXCL    0x08

static unsigned char load_config (Panel *panel)
{
    unsigned char changes = 0;
    char *tmp;
    int val;

    get_config_string (panel->dock ? "dock" : "panel", "widgets_left", &tmp, panel->dock ? WIDGETS_LEFT_DOCK : (panel->wizard ? WIDGETS_LEFT_WIZARD : WIDGETS_LEFT_PANEL));
    if (g_strcmp0 (tmp, panel->left_widgets_opt))
    {
        g_free (panel->left_widgets_opt);
        panel->left_widgets_opt = tmp;
        changes |= CFG_WIDGETS | CFG_EXCL;
    }
    else g_free (tmp);

    get_config_string (panel->dock ? "dock" : "panel", "widgets_right", &tmp, panel->dock ? WIDGETS_RIGHT_DOCK : (panel->wizard ? WIDGETS_RIGHT_WIZARD : WIDGETS_RIGHT_PANEL));
    if (g_strcmp0 (tmp, panel->right_widgets_opt))
    {
        g_free (panel->right_widgets_opt);
        panel->right_widgets_opt = tmp;
        changes |= CFG_WIDGETS | CFG_EXCL;
    }
    else g_free (tmp);

    val = get_config_int (panel->dock ? "dock" : "panel", "icon_size", panel->dock ? "48" : "32");
    if (panel->icon_size != val)
    {
        panel->icon_size = val;
        changes |= CFG_ICONS;
    }

    val = get_config_bool (panel->dock ? "dock" : "panel", "exclusive", panel->dock ? "false" : "true");
    if (panel->exclusive != val)
    {
        panel->exclusive = val;
        changes |= CFG_EXCL;
    }

    val = get_config_int ("notify", "timeout", "15");
    if (panel->notify_timeout != val)
    {
        panel->notify_timeout = val;
        changes |= CFG_NOTIFY;
    }
    val = get_config_bool ("notify", "enable", "true");
    if (panel->notifications != val)
    {
        panel->notifications = val;
        changes |= CFG_NOTIFY;
    }
    val = get_config_bool ("notify", "libnotify", "true");
    if (panel->libnotify != val)
    {
        panel->libnotify = val;
        changes |= CFG_NOTIFY;
    }

    gestures_touch_only = get_config_bool ("panel", "gestures_touch_only", "false");

    return changes;
}

// Set exclusive zone from the parameter value

static void set_exclusive (Panel *panel)
{
    if (panel->dock)
    {
        gtk_layer_set_anchor (GTK_WINDOW (panel->window->window), GTK_LAYER_SHELL_EDGE_LEFT, FALSE);
        gtk_layer_set_anchor (GTK_WINDOW (panel->window->window), GTK_LAYER_SHELL_EDGE_RIGHT, FALSE);
    }
    else
    {
        if (panel->wizard)
        {
            gtk_layer_set_anchor (GTK_WINDOW (panel->window->window), GTK_LAYER_SHELL_EDGE_LEFT, FALSE);
            gtk_layer_set_anchor (GTK_WINDOW (panel->window->window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
        }
        else if (panel->exclusive)
        {
            gtk_layer_set_anchor (GTK_WINDOW (panel->window->window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
            gtk_layer_set_anchor (GTK_WINDOW (panel->window->window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
        }
        else
        {
            gtk_layer_set_anchor (GTK_WINDOW (panel->window->window), GTK_LAYER_SHELL_EDGE_LEFT, panel->left_widgets ? TRUE : FALSE);
            gtk_layer_set_anchor (GTK_WINDOW (panel->window->window), GTK_LAYER_SHELL_EDGE_RIGHT, panel->right_widgets ? TRUE : FALSE);
        }
    }
    autohide_window_set_auto_exclusive_zone (panel->window, panel->exclusive);
}

// Keyboard and mouse event handlers

static gboolean on_keypress_event (GtkWidget *widget, GdkEventKey *event, gpointer userdata)
{
    Panel *panel = (Panel *) userdata;
    char *str = g_strdup_printf ("key_%c", event->keyval);
    GList *l;

    for (l = panel->left_widgets; l; l = l->next)
    {
        PanelWidget *w = (PanelWidget *) l->data;
        if (!g_strcmp0 (w->widget_name, "smenu") && w->widget_command) w->widget_command (w, str);
    }

    for (l = panel->right_widgets; l; l = l->next)
    {
        PanelWidget *w = (PanelWidget *) l->data;
        if (!g_strcmp0 (w->widget_name, "smenu") && w->widget_command) w->widget_command (w, str);
    }

    g_free (str);

    return FALSE;
}

static gboolean on_button_press_event (GtkWidget *widget, GdkEventButton *event, gpointer userdata)
{
    pressed = PRESS_SHORT;

    return FALSE;
}

static void show_menu (Panel *panel, GtkWidget *plugin, GdkEventButton *event, gboolean *found)
{
    GtkAllocation alloc;
    const char *pname;
    char *title = NULL;

    if (gtk_widget_get_visible (plugin))
    {
        // check if the position of the mouse is within the plugin
        gtk_widget_get_allocation (plugin, &alloc);
        if (event->x_root >= alloc.x && event->x_root < alloc.x + alloc.width &&
            event->y_root >= alloc.y && event->y_root < alloc.y + alloc.height)
        {
            pname = gtk_widget_get_name (plugin);
            gtk_widget_set_name (panel->cplug, pname);
            if (can_configure (pname, &title) && g_strcmp0 (pname, "spacing") && g_strcmp0 (pname, "separator"))
            {
                gtk_widget_show (panel->cplug);
                gtk_widget_show (panel->sep);
            }
            gtk_widget_set_sensitive (panel->cplug, cdlg ? FALSE : TRUE);
            if (title)
            {
                gtk_menu_item_set_label (GTK_MENU_ITEM (panel->cplug), title);
                g_free (title);
            }
            show_menu_with_kbd (plugin, panel->menu, event);
            *found = TRUE;
        }
    }
}

static gboolean on_button_release_event (GtkWidget *widget, GdkEventButton *event, gpointer userdata)
{
    Panel *panel = (Panel *) userdata;
    gboolean found = FALSE;
    GList *children, *l;

    if (pressed == PRESS_NONE) return FALSE;
    pressed = PRESS_NONE;

    if (event->button == 3)
    {
        gtk_widget_set_name (panel->cplug, "gtkmm");
        gtk_widget_hide (panel->cplug);
        gtk_widget_hide (panel->sep);

        children = gtk_container_get_children (GTK_CONTAINER (panel->left_box));
        for (l = children; l; l = l->next) show_menu (panel, GTK_WIDGET (l->data), event, &found);
        g_list_free (children);

        children = gtk_container_get_children (GTK_CONTAINER (panel->right_box));
        for (l = children; l; l = l->next) show_menu (panel, GTK_WIDGET (l->data), event, &found);
        g_list_free (children);

        if (panel->dock)
        {
            children = gtk_container_get_children (GTK_CONTAINER (panel->right2_box));
            for (l = children; l; l = l->next) show_menu (panel, GTK_WIDGET (l->data), event, &found);
            g_list_free (children);
        }

        // not matched any widgets - on the empty area of the bar...
        if (!found) show_menu_with_kbd_at_xy (panel->window->window, panel->menu, event);
    }
    return FALSE;
}

// Window close handler

static gboolean on_delete (GtkWidget *widget, GdkEventAny *ev, gpointer userdata)
{
    return TRUE;
}

// Menu event handlers

static void do_plugin_configure (GtkWidget *widget, gpointer userdata)
{
    Panel *panel = (Panel *) userdata;

    plugin_config_dialog (gtk_widget_get_name (panel->cplug));
}

static void do_configure (GtkWidget *widget, gpointer userdata)
{
    Panel *panel = (Panel *) userdata;

    if (panel->dock) system ("rpcc widgets set_dock &");
    else system ("rpcc widgets set_bar &");
}

static void do_notify_configure (GtkWidget *widget, gpointer userdata)
{
    system ("rpcc notifications &");
}

static void do_appearance_set (GtkWidget *widget, gpointer userdata)
{
    Panel *panel = (Panel *) userdata;

    if (panel->dock) system ("rpcc dock &");
    else system ("rpcc taskbar &");
}

// Widget loading

static PanelWidget *widget_from_name (const char *name)
{
    if (strstr (name, "spacing"))
    {
        int width;
        if (sscanf (name + 7, "%d", &width) != 1 || width < 0) return NULL;
        char *libname = g_strdup_printf (width ? PLUGIN_PATH "libspacing.so" : PLUGIN_PATH "libseparator.so");
        void *wid = dlopen (libname, RTLD_LAZY);
        g_free (libname);
        if (wid)
        {
            if (width)
            {
                PanelWidget *(*create_widget) (int) = (PanelWidget *(*) (int)) dlsym (wid, "create");
                return create_widget (width);
            }
            else
            {
                PanelWidget *(*create_widget) (void) = (PanelWidget *(*) (void)) dlsym (wid, "create");
                return create_widget ();
            }
        }
    }

    if (g_strcmp0 (name, "none"))
    {
        char *libname = g_strdup_printf (PLUGIN_PATH "lib%s.so", name);
        void *wid = dlopen (libname, RTLD_LAZY);
        g_free (libname);
        if (wid)
        {
            PanelWidget *(*create_widget) (void) = (PanelWidget *(*) (void)) dlsym (wid, "create");
            return create_widget ();
        }
        else printf ("error loading widget %s : %s\n", name, dlerror ());
    }
    return NULL;
}

static void free_panel_widget (PanelWidget *widget)
{
    if (widget->widget_free) widget->widget_free (widget);
    g_free (widget->widget_name);
    g_free (widget);
}

static void reload_widgets (const char *list, GList **container, GtkWidget *box)
{
    GList *l;
    char **tokens;
    int i;

    for (l = *container; l; l = l->next) free_panel_widget ((PanelWidget *) l->data);
    g_list_free (*container);
    *container = NULL;

    tokens = g_strsplit_set (list, " \t\n\r\f\v", -1);
    for (i = 0; tokens[i]; i++)
    {
        PanelWidget *widget;

        if (!tokens[i][0]) continue;

        widget = widget_from_name (tokens[i]);
        if (!widget) continue;

        widget->widget_name = g_strdup (tokens[i]);
        if (widget->widget_init) widget->widget_init (widget, box);
        *container = g_list_append (*container, widget);

        // a badly-written widget could reset the textdomain to a local value - reset back to the system value after each load
        textdomain (GETTEXT_PACKAGE);
    }
    g_strfreev (tokens);
}

static void init_widgets (Panel *panel)
{
    reload_widgets (panel->left_widgets_opt, &panel->left_widgets, panel->left_box);
    reload_widgets (panel->right_widgets_opt, &panel->right_widgets, panel->right_box);
    if ((!panel->left_widgets_opt || !panel->left_widgets_opt[0]) && (!panel->right_widgets_opt || !panel->right_widgets_opt[0]))
        gtk_widget_hide (panel->window->window);
    else gtk_widget_show (panel->window->window);
    set_exclusive (panel);
}

// Set up notifications and callbacks

static void init_notify (Panel *panel)
{
    if (!panel->dock) wfpanel_notify_init (panel->notifications, panel->libnotify, panel->notify_timeout, GTK_WINDOW (panel->window->window));
}

// Update all displayed icons

static void update_widget_icons (Panel *panel)
{
    GList *l;

    for (l = panel->left_widgets; l; l = l->next)
    {
        PanelWidget *w = (PanelWidget *) l->data;
        if (w->widget_set_icon) w->widget_set_icon (w);
    }

    for (l = panel->right_widgets; l; l = l->next)
    {
        PanelWidget *w = (PanelWidget *) l->data;
        if (w->widget_set_icon) w->widget_set_icon (w);
    }

    autohide_window_update_position (panel->window);
}

// Public functions used by PanelApp

void panel_handle_config_reload (Panel *panel)
{
    unsigned char changes = load_config (panel);

    if (changes & CFG_EXCL) set_exclusive (panel);
    if (changes & CFG_NOTIFY) init_notify (panel);
    if (changes & CFG_WIDGETS)
    {
        close_popup ();
        gtk_menu_popdown (GTK_MENU (panel->menu));
        reload = TRUE;
        init_widgets (panel);
        reload = FALSE;
    }
    if (changes & CFG_ICONS || changes & CFG_WIDGETS) update_widget_icons (panel);

    autohide_window_handle_config_reload (panel->window);

    GList *l;

    for (l = panel->left_widgets; l; l = l->next)
    {
        PanelWidget *w = (PanelWidget *) l->data;
        if (w->widget_config_reload) w->widget_config_reload (w);
    }

    for (l = panel->right_widgets; l; l = l->next)
    {
        PanelWidget *w = (PanelWidget *) l->data;
        if (w->widget_config_reload) w->widget_config_reload (w);
    }
}

void panel_handle_command_message (Panel *panel, const char *name, const char *cmd)
{
    if (!g_strcmp0 (name, "notify"))
    {
        wfpanel_notify (cmd);
        return;
    }

    if (!g_strcmp0 (name, "critical"))
    {
        wfpanel_critical (cmd);
        return;
    }

    if (!gtk_widget_get_sensitive (panel->window->window)) return;

    GList *l;

    for (l = panel->left_widgets; l; l = l->next)
    {
        PanelWidget *w = (PanelWidget *) l->data;
        if (!fnmatch (name, w->widget_name, 0) && w->widget_command) w->widget_command (w, cmd);
    }

    for (l = panel->right_widgets; l; l = l->next)
    {
        PanelWidget *w = (PanelWidget *) l->data;
        if (!fnmatch (name, w->widget_name, 0) && w->widget_command) w->widget_command (w, cmd);
    }
}

void panel_monitor_update_pending (Panel *panel, gboolean pend)
{
    panel->pending_update = pend;
}

static gboolean on_draw_log_first (GtkWidget *widget, cairo_t *cr, gpointer userdata)
{
    Panel *panel = (Panel *) userdata;

    // Log first draw event directly to journald with minimal information
    GLogField fields[] = {
        {"MESSAGE", "Panel first draw event", -1},
        {"PRIORITY", "5", -1}, // Notice level
        {"SYSLOG_IDENTIFIER", "wf-panel-pi", -1}};
    g_log_writer_journald (G_LOG_LEVEL_MESSAGE, fields, 3, NULL);

    // Disconnect after first draw
    g_signal_handler_disconnect (panel->window->window, panel->draw_connection);
    panel->draw_connection = 0;
    // Return false to propagate the event further
    return FALSE;
}

static gboolean on_draw_dock_layout (GtkWidget *widget, cairo_t *cr, gpointer userdata)
{
    Panel *panel = (Panel *) userdata;
    int scale_now;

    if (panel->pending_update) return FALSE;
    scale_now = gtk_widget_get_scale_factor (panel->window->window);
    if (panel->scaling != scale_now)
    {
        panel->scaling = scale_now;
        update_widget_icons (panel);
    }

    if (panel->dock && panel->right_widgets)
    {
        // organise the dock tray widgets so the bottom is never wider than the top, and the overall width is as narrow as possible...
        GtkRequisition min, pref;
        GtkWidget *plugin;
        GList *children, *l;
        int top, last, btm;
        gboolean split;

        while (1)
        {
            top = 0;
            btm = 0;
            last = 0;
            split = FALSE;

            children = gtk_container_get_children (GTK_CONTAINER (panel->right_box));
            for (l = children; l; l = l->next)
            {
                GtkWidget *w = GTK_WIDGET (l->data);
                if (!g_strcmp0 (gtk_widget_get_name (w), "split")) split = TRUE;
                gtk_widget_get_preferred_size (w, &min, &pref);
                last = pref.width;
                top += pref.width;
            }
            g_list_free (children);

            if (split)
            {
                while (1)
                {
                    children = gtk_container_get_children (GTK_CONTAINER (panel->right_box));
                    plugin = GTK_WIDGET (g_list_last (children)->data);
                    g_list_free (children);
                    if (!g_strcmp0 (gtk_widget_get_name (plugin), "split")) break;
                    gtk_container_remove (GTK_CONTAINER (panel->right_box), plugin);
                    gtk_box_pack_start (GTK_BOX (panel->right2_box), plugin, FALSE, FALSE, 0);
                    gtk_box_reorder_child (GTK_BOX (panel->right2_box), plugin, 0);
                }
                break;
            }

            children = gtk_container_get_children (GTK_CONTAINER (panel->right2_box));
            for (l = children; l; l = l->next)
            {
                GtkWidget *w = GTK_WIDGET (l->data);
                gtk_widget_get_preferred_size (w, &min, &pref);
                btm += pref.width;
            }
            g_list_free (children);

            if (btm == 0 && top == 0) break;

            if (btm > top)
            {
                // move up
                children = gtk_container_get_children (GTK_CONTAINER (panel->right2_box));
                plugin = GTK_WIDGET (children->data);
                g_list_free (children);
                gtk_container_remove (GTK_CONTAINER (panel->right2_box), plugin);
                gtk_box_pack_end (GTK_BOX (panel->right_box), plugin, FALSE, FALSE, 0);
                gtk_box_reorder_child (GTK_BOX (panel->right_box), plugin, 0);
            }
            else if (top - last >= btm + last)
            {
                // move down
                children = gtk_container_get_children (GTK_CONTAINER (panel->right_box));
                plugin = GTK_WIDGET (g_list_last (children)->data);
                g_list_free (children);
                gtk_container_remove (GTK_CONTAINER (panel->right_box), plugin);
                gtk_box_pack_start (GTK_BOX (panel->right2_box), plugin, FALSE, FALSE, 0);
                gtk_box_reorder_child (GTK_BOX (panel->right2_box), plugin, 0);
            }
            else break;
        }
    }

    return FALSE;
}

static void on_gesture_pressed (GtkGesture *gesture, gdouble x, gdouble y, gpointer userdata)
{
    pressed = PRESS_LONG;
    press_x = x;
    press_y = y;
}

static void on_gesture_end (GtkGesture *gesture, GdkEventSequence *sequence, gpointer userdata)
{
    Panel *panel = (Panel *) userdata;

    if (pressed == PRESS_LONG) pass_right_click (panel->window->window, press_x, press_y);
}

/* End of file */
/*----------------------------------------------------------------------------*/

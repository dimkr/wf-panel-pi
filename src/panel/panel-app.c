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

#include <fcntl.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <gio/gio.h>
#include <menu-cache.h>

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>

#include "panel.h"

#include "panel-app.h"

static const gchar introspection_xml[] =
  "<node>"
  "  <interface name='com.raspberrypi.wfpanelpi'>"
  "    <method name='command'>"
  "      <arg type='s' name='plugin' direction='in'/>"
  "      <arg type='s' name='command' direction='in'/>"
  "    </method>"
  "  </interface>"
  "</node>";

static PanelApp *instance;
gboolean activated = FALSE;
char *confdir, *conffile;
MenuCache *mcache, *mcache_h;

/*----------------------------------------------------------------------------*/
/* Prototypes                                                                 */
/*----------------------------------------------------------------------------*/

static void panel_app_run (PanelApp *app, int argc, char **argv);
static void on_activate (GApplication *gapp, gpointer userdata);
static void on_menu_cache_reload (MenuCache *cache, gpointer user_data);
static gboolean handle_inotify_event (GIOChannel *source, GIOCondition cond, gpointer userdata);
static void monitors_changed (PanelApp *app);
static void update_monitors (PanelApp *app);
static gboolean handle_hotplug (gpointer userdata);
static void on_bus_acquired (GDBusConnection *connection, const gchar *name, gpointer userdata);
static void on_name_acquired (GDBusConnection *connection, const gchar *name, gpointer userdata);
static void on_name_lost (GDBusConnection *connection, const gchar *name, gpointer userdata);
static void handle_method_call (GDBusConnection *connection, const gchar *sender, const gchar *object_path,
    const gchar *interface_name, const gchar *method_name, GVariant *parameters,
    GDBusMethodInvocation *invocation, gpointer userdata);
static void on_monitor_added (GdkDisplay *display, GdkMonitor *monitor, gpointer userdata);
static void on_monitor_removed (GdkDisplay *display, GdkMonitor *monitor, gpointer userdata);

static PanelApp *panel_app_new (int argc, char **argv)
{
    PanelApp *app = g_new0 (PanelApp, 1);

    app->app = gtk_application_new ("com.raspberrypi.wfpanelpi", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect (app->app, "activate", G_CALLBACK (on_activate), app);

    return app;
}

static void panel_app_free (PanelApp *app)
{
    if (app->owner_id) g_bus_unown_name (app->owner_id);

    if (app->panel) panel_free (app->panel);
    if (app->dock) panel_free (app->dock);

    if (app->introspection_data) g_dbus_node_info_unref (app->introspection_data);

    g_object_unref (app->app);

    g_free (app);
}

void panel_app_create (int argc, char **argv)
{
    instance = panel_app_new (argc, argv);
    panel_app_run (instance, argc, argv);
    panel_app_free (instance);
    instance = NULL;
}

static void panel_app_run (PanelApp *app, int argc, char **argv)
{
    g_application_run (G_APPLICATION (app->app), argc, argv);
}

static void on_activate (GApplication *gapp, gpointer userdata)
{
    PanelApp *app = (PanelApp *) userdata;
    GdkDisplay *display;
    struct wl_display *wl_display;
    GIOChannel *channel;
    gboolean need_prefix;

    if (activated) return;
    activated = TRUE;

    g_application_hold (gapp);

    display = gdk_display_get_default ();
    wl_display = gdk_wayland_display_get_wl_display (display);
    if (!wl_display)
    {
        fprintf (stderr, "No Wayland display found\n");
        exit (-1);
    }

    // create a config file to track if it doesn't exist
    confdir = g_build_filename (g_get_user_config_dir (), "wf-panel-pi", NULL);
    if (!g_strcmp0 (getenv ("USER"), "rpi-first-boot-wizard"))
        conffile = g_build_filename (confdir, "wizard.ini", NULL);
    else
        conffile = g_build_filename (confdir, "wf-panel-pi.ini", NULL);

    g_mkdir_with_parents (confdir, S_IRUSR | S_IWUSR | S_IXUSR);
    close (open (conffile, O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH));

    // setup config file tracking
    app->inotify_fd = inotify_init ();
    channel = g_io_channel_unix_new (app->inotify_fd);
    app->inotify_source = g_io_add_watch (channel, G_IO_IN | G_IO_HUP, handle_inotify_event, app);
    g_io_channel_unref (channel);
    inotify_add_watch (app->inotify_fd, conffile, IN_MODIFY);
    inotify_add_watch (app->inotify_fd, confdir, IN_CREATE | IN_DELETE);

    // create menu caches
    need_prefix = (g_getenv ("XDG_MENU_PREFIX") == NULL);
    mcache = menu_cache_lookup_sync (need_prefix ? "lxde-applications.menu" : "applications.menu");
    mcache_h = menu_cache_lookup_sync (need_prefix ? "lxde-applications.menu+hidden" : "applications.menu+hidden");

    // unless there is a notify defined for the menu cache, it never updates...
    menu_cache_add_reload_notify (mcache, on_menu_cache_reload, NULL);
    menu_cache_add_reload_notify (mcache_h, on_menu_cache_reload, NULL);

    // setup monitor tracking
    g_signal_connect (display, "monitor-added", G_CALLBACK (on_monitor_added), app);
    g_signal_connect (display, "monitor-removed", G_CALLBACK (on_monitor_removed), app);

    // load initial monitors
    update_monitors (app);

    // own on DBus
    app->introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
    app->owner_id = g_bus_own_name (G_BUS_TYPE_SESSION, "com.raspberrypi.wfpanelpi", G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired, on_name_acquired, on_name_lost, app, NULL);
}

/* Menu cache reload tracking */

static void on_menu_cache_reload (MenuCache *cache, gpointer user_data)
{
    /* this space intentionally blank... */
}

/* Config file change tracking */

static gboolean handle_inotify_event (GIOChannel *source, GIOCondition cond, gpointer userdata)
{
    PanelApp *app = (PanelApp *) userdata;
    char buf[1024 * sizeof (struct inotify_event)];

    read (app->inotify_fd, buf, sizeof (buf));

    if (app->panel) panel_handle_config_reload (app->panel);
    if (app->dock) panel_handle_config_reload (app->dock);

    inotify_add_watch (app->inotify_fd, conffile, IN_MODIFY);
    inotify_add_watch (app->inotify_fd, confdir, IN_CREATE | IN_DELETE);

    return TRUE;
}

/* Monitor tracking */

static void on_monitor_added (GdkDisplay *display, GdkMonitor *monitor, gpointer userdata)
{
    monitors_changed ((PanelApp *) userdata);
}

static void on_monitor_removed (GdkDisplay *display, GdkMonitor *monitor, gpointer userdata)
{
    monitors_changed ((PanelApp *) userdata);
}

static void monitors_changed (PanelApp *app)
{
    if (app->hotplug_timer)
    {
        g_source_remove (app->hotplug_timer);
        app->hotplug_timer = 0;
    }

    panel_monitor_update_pending (app->panel, TRUE);
    panel_monitor_update_pending (app->dock, TRUE);

    app->hotplug_timer = g_timeout_add (500, handle_hotplug, app);
}

static void update_monitors (PanelApp *app)
{
    if (app->panel) autohide_window_set_monitor (app->panel->window);
    else app->panel = panel_new (FALSE);
    if (app->dock) autohide_window_set_monitor (app->dock->window);
    else app->dock = panel_new (TRUE);

    panel_monitor_update_pending (app->panel, FALSE);
    panel_monitor_update_pending (app->dock, FALSE);
}

static gboolean handle_hotplug (gpointer userdata)
{
    PanelApp *app = (PanelApp *) userdata;

    app->hotplug_timer = 0;
    update_monitors (app);
    system ("if pgrep swaybg > /dev/null ; then pkill swaybg ; fi");

    return G_SOURCE_REMOVE;
}

/* DBus interface for commands to plugins */

static void on_bus_acquired (GDBusConnection *connection, const gchar *name, gpointer userdata)
{
    PanelApp *app = (PanelApp *) userdata;

    app->interface_vtable = g_new0 (GDBusInterfaceVTable, 1);
    app->interface_vtable->method_call = handle_method_call;

    g_dbus_connection_register_object (connection, "/com/raspberrypi/wfpanelpi",
        app->introspection_data->interfaces[0], app->interface_vtable, app, NULL, NULL);
}

static void on_name_acquired (GDBusConnection *connection, const gchar *name, gpointer userdata)
{
}

static void on_name_lost (GDBusConnection *connection, const gchar *name, gpointer userdata)
{
}

static void handle_method_call (GDBusConnection *connection, const gchar *sender, const gchar *object_path,
    const gchar *interface_name, const gchar *method_name, GVariant *parameters,
    GDBusMethodInvocation *invocation, gpointer userdata)
{
    PanelApp *app = (PanelApp *) userdata;
    const gchar *plugin, *command;

    if (!g_strcmp0 (method_name, "command"))
    {
        g_variant_get (parameters, "(&s&s)", &plugin, &command);

        if (app->panel) panel_handle_command_message (app->panel, plugin, command);
        if (app->dock) panel_handle_command_message (app->dock, plugin, command);
    }
}

int main (int argc, char **argv)
{
    setlocale (LC_ALL, "");
    bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    panel_app_create (argc, argv);
    return 0;
}

/* End of file */
/*----------------------------------------------------------------------------*/

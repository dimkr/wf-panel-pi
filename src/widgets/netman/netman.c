#include <locale.h>
#include <glib/gi18n.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plugin.h"
#include "netman.h"

/*----------------------------------------------------------------------------*/
/* Typedefs and macros                                                        */
/*----------------------------------------------------------------------------*/

#define REFRESH_INTERVAL 3000

typedef enum {
    NET_TYPE_OFFLINE = 0,
    NET_TYPE_ETHERNET,
    NET_TYPE_WIFI
} NetDeviceType;

typedef struct {
    NetDeviceType type;
    int strength;
    char ifname[256];
    char ssid[128];
} NetStatus;

/*----------------------------------------------------------------------------*/
/* Global data                                                                */
/*----------------------------------------------------------------------------*/

conf_table_t conf_table[1] = {
    {CONF_TYPE_NONE, NULL, NULL, NULL, NULL}
};

/*----------------------------------------------------------------------------*/
/* Function definitions                                                       */
/*----------------------------------------------------------------------------*/

static void parse_connman_ethernet (GVariant *eth_var, char *ifname, size_t ifname_len)
{
    if (!eth_var) return;
    GVariantIter iter;
    g_variant_iter_init (&iter, eth_var);
    const char *key;
    GVariant *val;
    while (g_variant_iter_loop (&iter, "{sv}", &key, &val))
    {
        if (g_strcmp0 (key, "Interface") == 0 && g_variant_is_of_type (val, G_VARIANT_TYPE_STRING))
        {
            snprintf (ifname, ifname_len, "%s", g_variant_get_string (val, NULL));
        }
    }
}

static gboolean get_status_from_connman (NetStatus *status)
{
    GError *error = NULL;
    GDBusConnection *bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!bus)
    {
        if (error) g_error_free (error);
        return FALSE;
    }

    GVariant *res = g_dbus_connection_call_sync (
        bus,
        "net.connman",
        "/",
        "net.connman.Manager",
        "GetServices",
        NULL,
        G_VARIANT_TYPE ("(a(oa{sv}))"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error
    );

    g_object_unref (bus);

    if (!res)
    {
        if (error) g_error_free (error);
        return FALSE;
    }

    GVariant *services_arr = g_variant_get_child_value (res, 0);
    GVariantIter iter;
    g_variant_iter_init (&iter, services_arr);

    GVariant *service_tuple;
    gboolean found = FALSE;

    while (!found && (service_tuple = g_variant_iter_next_value (&iter)))
    {
        GVariant *props = g_variant_get_child_value (service_tuple, 1);
        GVariantIter props_iter;
        g_variant_iter_init (&props_iter, props);

        const char *key;
        GVariant *val;

        char state_str[32] = {0};
        char type_str[32] = {0};
        char name_str[128] = {0};
        char ifname_str[256] = {0};
        int strength = 0;

        while (g_variant_iter_loop (&props_iter, "{sv}", &key, &val))
        {
            if (g_strcmp0 (key, "State") == 0 && g_variant_is_of_type (val, G_VARIANT_TYPE_STRING))
            {
                snprintf (state_str, sizeof (state_str), "%s", g_variant_get_string (val, NULL));
            }
            else if (g_strcmp0 (key, "Type") == 0 && g_variant_is_of_type (val, G_VARIANT_TYPE_STRING))
            {
                snprintf (type_str, sizeof (type_str), "%s", g_variant_get_string (val, NULL));
            }
            else if (g_strcmp0 (key, "Name") == 0 && g_variant_is_of_type (val, G_VARIANT_TYPE_STRING))
            {
                snprintf (name_str, sizeof (name_str), "%s", g_variant_get_string (val, NULL));
            }
            else if (g_strcmp0 (key, "Strength") == 0)
            {
                if (g_variant_is_of_type (val, G_VARIANT_TYPE_BYTE))
                    strength = g_variant_get_byte (val);
                else if (g_variant_is_of_type (val, G_VARIANT_TYPE_INT32))
                    strength = g_variant_get_int32 (val);
                else if (g_variant_is_of_type (val, G_VARIANT_TYPE_UINT32))
                    strength = g_variant_get_uint32 (val);
            }
            else if (g_strcmp0 (key, "Ethernet") == 0)
            {
                parse_connman_ethernet (val, ifname_str, sizeof (ifname_str));
            }
        }

        if (g_strcmp0 (state_str, "online") == 0 || g_strcmp0 (state_str, "ready") == 0)
        {
            if (g_strcmp0 (type_str, "wifi") == 0)
            {
                status->type = NET_TYPE_WIFI;
                status->strength = strength;
                snprintf (status->ssid, sizeof (status->ssid), "%s", name_str);
                snprintf (status->ifname, sizeof (status->ifname), "%s", ifname_str);
                found = TRUE;
            }
            else if (g_strcmp0 (type_str, "ethernet") == 0)
            {
                status->type = NET_TYPE_ETHERNET;
                snprintf (status->ifname, sizeof (status->ifname), "%s", ifname_str[0] ? ifname_str : "eth0");
                found = TRUE;
            }
        }

        g_variant_unref (props);
        g_variant_unref (service_tuple);
    }

    g_variant_unref (services_arr);
    g_variant_unref (res);

    return found;
}

static void get_status_from_sysfs (NetStatus *status)
{
    DIR *dir = opendir ("/sys/class/net");
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir (dir)) != NULL)
    {
        if (entry->d_name[0] == '.' || g_strcmp0 (entry->d_name, "lo") == 0)
            continue;

        char path[512];
        snprintf (path, sizeof (path), "/sys/class/net/%s/operstate", entry->d_name);
        FILE *f = fopen (path, "r");
        if (!f) continue;

        char state[32] = {0};
        if (fgets (state, sizeof (state), f))
        {
            g_strstrip (state);
        }
        fclose (f);

        if (g_strcmp0 (state, "up") == 0 || g_strcmp0 (state, "unknown") == 0)
        {
            /* Check if wireless */
            snprintf (path, sizeof (path), "/sys/class/net/%s/wireless", entry->d_name);
            gboolean is_wireless = g_file_test (path, G_FILE_TEST_EXISTS);

            if (!is_wireless)
            {
                snprintf (path, sizeof (path), "/sys/class/net/%s/phy80211", entry->d_name);
                is_wireless = g_file_test (path, G_FILE_TEST_EXISTS);
            }

            if (is_wireless)
            {
                status->type = NET_TYPE_WIFI;
                snprintf (status->ifname, sizeof (status->ifname), "%s", entry->d_name);
                status->strength = 100;

                FILE *wf = fopen ("/proc/net/wireless", "r");
                if (wf)
                {
                    char line[256];
                    while (fgets (line, sizeof (line), wf))
                    {
                        if (strstr (line, entry->d_name))
                        {
                            int link_qual = 0;
                            char *ptr = strchr (line, ':');
                            if (ptr && sscanf (ptr + 1, "%*s %d", &link_qual) == 1)
                            {
                                status->strength = (link_qual * 100) / 70;
                                if (status->strength > 100) status->strength = 100;
                            }
                            break;
                        }
                    }
                    fclose (wf);
                }
                break;
            }
            else
            {
                status->type = NET_TYPE_ETHERNET;
                snprintf (status->ifname, sizeof (status->ifname), "%s", entry->d_name);
                break;
            }
        }
    }
    closedir (dir);
}

static void update_net_status (NetmanPlugin *net)
{
    NetStatus status;
    memset (&status, 0, sizeof (status));
    status.type = NET_TYPE_OFFLINE;

    if (!get_status_from_connman (&status))
    {
        get_status_from_sysfs (&status);
    }

    char label_buf[64];
    char tooltip_buf[512];

    if (status.type == NET_TYPE_WIFI)
    {
        /* format-wifi: "\uf1eb {signalStrength}%" */
        snprintf (label_buf, sizeof (label_buf), "\uf1eb %d%%", status.strength);

        /* tooltip-format-wifi: "{essid}" */
        if (status.ssid[0] && status.ifname[0])
            snprintf (tooltip_buf, sizeof (tooltip_buf), "%s (%s)", status.ssid, status.ifname);
        else if (status.ssid[0])
            snprintf (tooltip_buf, sizeof (tooltip_buf), "%s", status.ssid);
        else if (status.ifname[0])
            snprintf (tooltip_buf, sizeof (tooltip_buf), "%s", status.ifname);
        else
            snprintf (tooltip_buf, sizeof (tooltip_buf), "%s", _("WiFi"));
    }
    else if (status.type == NET_TYPE_ETHERNET)
    {
        /* format-ethernet: "\uf0c1 wired" */
        snprintf (label_buf, sizeof (label_buf), "\uf0c1 wired");

        /* tooltip-format: "{ifname}" */
        if (status.ifname[0])
            snprintf (tooltip_buf, sizeof (tooltip_buf), "%s", status.ifname);
        else
            snprintf (tooltip_buf, sizeof (tooltip_buf), "%s", _("Ethernet"));
    }
    else
    {
        /* format: "\uf127 offline" */
        snprintf (label_buf, sizeof (label_buf), "\uf127 offline");
        snprintf (tooltip_buf, sizeof (tooltip_buf), "%s", _("Offline"));
    }

    gtk_label_set_text (GTK_LABEL (net->label), label_buf);
    gtk_widget_set_tooltip_text (net->plugin, tooltip_buf);
    gtk_widget_show_all (net->plugin);
}

static gboolean timer_event (NetmanPlugin *net)
{
    update_net_status (net);
    return TRUE;
}

static void netman_button_clicked (GtkWidget *widget, NetmanPlugin *net)
{
    CHECK_LONGPRESS
    g_spawn_command_line_async ("connman-gtk", NULL);
}

void netman_update_display (NetmanPlugin *net)
{
    update_net_status (net);
}

void netman_init (NetmanPlugin *net)
{
    setlocale (LC_ALL, "");
    bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

    net->label = gtk_label_new (NULL);
    gtk_widget_set_margin_start (net->label, 4);
    gtk_widget_set_margin_end (net->label, 4);
    gtk_label_set_xalign (GTK_LABEL (net->label), 0.5);

    gtk_container_add (GTK_CONTAINER (net->plugin), net->label);
    gtk_button_set_relief (GTK_BUTTON (net->plugin), GTK_RELIEF_NONE);

    g_signal_connect (net->plugin, "clicked", G_CALLBACK (netman_button_clicked), net);
    wrap_add_longpress (net->gesture, net->plugin, NULL, NULL);

    update_net_status (net);
    net->timer = g_timeout_add (REFRESH_INTERVAL, (GSourceFunc) timer_event, net);
}

void netman_destructor (gpointer user_data)
{
    NetmanPlugin *net = (NetmanPlugin *) user_data;

    wrap_free_gesture (net->gesture);

    if (net->timer)
        g_source_remove (net->timer);

    g_free (net);
}

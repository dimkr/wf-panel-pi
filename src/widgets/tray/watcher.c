#include "watcher.h"

#include <gio/gio.h>

#include <stdio.h>
#include <string.h>

struct _Watcher
{
    int refcount;

    guint dbus_name_id;
    guint dbus_object_id;
    GDBusConnection *watcher_connection;

    GHashTable *sn_items_id; /* char * (owned) -> guint (watch id) */
    GHashTable *sn_hosts_id; /* char * (owned) -> guint (watch id) */

    GDBusInterfaceVTable interface_table;
};

static Watcher *g_watcher_instance;

static const gchar introspection_xml[] =
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
"<node name=\"/StatusNotifierWatcher\">\n"
"    <interface name=\"org.kde.StatusNotifierWatcher\">\n"
"        <method name=\"RegisterStatusNotifierItem\">\n"
"            <arg direction=\"in\" name=\"service\" type=\"s\"/>\n"
"        </method>\n"
"        <method name=\"RegisterStatusNotifierHost\">\n"
"            <arg direction=\"in\" name=\"service\" type=\"s\"/>\n"
"        </method>\n"
"\n"
"        <property name=\"RegisteredStatusNotifierItems\" type=\"as\" access=\"read\"/>\n"
"        <property name=\"IsStatusNotifierHostRegistered\" type=\"b\" access=\"read\"/>\n"
"        <property name=\"ProtocolVersion\" type=\"i\" access=\"read\"/>\n"
"\n"
"        <signal name=\"StatusNotifierItemRegistered\">\n"
"            <arg name=\"service\" type=\"s\"/>\n"
"        </signal>\n"
"        <signal name=\"StatusNotifierItemUnregistered\">\n"
"            <arg name=\"service\" type=\"s\"/>\n"
"        </signal>\n"
"        <signal name=\"StatusNotifierHostRegistered\"/>\n"
"    </interface>\n"
"</node>\n";

static GDBusInterfaceInfo *introspection_data;

/*----------------------------------------------------------------------------*/
/* Prototypes                                                                 */
/*----------------------------------------------------------------------------*/

static GDBusInterfaceInfo *get_introspection_data (void);
static void emit_signal_s (Watcher *w, const char *name, const char *arg);
static void emit_signal0 (Watcher *w, const char *name);
static void on_bus_acquired (GDBusConnection *connection, const gchar *name, gpointer userdata);
static void item_watch_ctx_free (gpointer data);
static void on_item_name_vanished (GDBusConnection *connection, const gchar *name, gpointer userdata);
static void register_status_notifier_item (Watcher *w, GDBusConnection *connection, const char *sender, const char *path);
static void on_host_name_appeared (GDBusConnection *connection, const gchar *name, const gchar *name_owner, gpointer userdata);
static void on_host_name_vanished (GDBusConnection *connection, const gchar *name, gpointer userdata);
static void register_status_notifier_host (Watcher *w, GDBusConnection *connection, const char *service);
static void on_interface_method_call (GDBusConnection *connection, const gchar *sender, const gchar *object_path,
    const gchar *interface_name, const gchar *method_name, GVariant *parameters,
    GDBusMethodInvocation *invocation, gpointer userdata);
static GVariant *get_registred_items (Watcher *w);
static GVariant *on_interface_get_property (GDBusConnection *connection, const gchar *sender, const gchar *object_path,
    const gchar *interface_name, const gchar *property_name, GError **error, gpointer userdata);

static GDBusInterfaceInfo *get_introspection_data (void)
{
    if (!introspection_data)
    {
        GDBusNodeInfo *node_info = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
        introspection_data = g_dbus_interface_info_ref (node_info->interfaces[0]);
        g_dbus_node_info_unref (node_info);
    }
    return introspection_data;
}

static void emit_signal_s (Watcher *w, const char *name, const char *arg)
{
    g_dbus_connection_emit_signal (w->watcher_connection, NULL, SNW_PATH, SNW_IFACE, name,
        g_variant_new ("(s)", arg), NULL);
}

static void emit_signal0 (Watcher *w, const char *name)
{
    g_dbus_connection_emit_signal (w->watcher_connection, NULL, SNW_PATH, SNW_IFACE, name, NULL, NULL);
}

Watcher *watcher_launch (void)
{
    Watcher *w;

    if (!g_watcher_instance)
    {
        w = g_new0 (Watcher, 1);
        w->refcount = 1;
        w->sn_items_id = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
        w->sn_hosts_id = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
        w->interface_table.method_call = on_interface_method_call;
        w->interface_table.get_property = on_interface_get_property;
        w->interface_table.set_property = NULL;
        w->dbus_name_id = g_bus_own_name (G_BUS_TYPE_SESSION, SNW_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
            on_bus_acquired, NULL, NULL, w, NULL);

        g_watcher_instance = w;
        return w;
    }

    return watcher_instance ();
}

Watcher *watcher_instance (void)
{
    if (g_watcher_instance) g_watcher_instance->refcount++;
    return g_watcher_instance;
}

void watcher_unref (Watcher *watcher)
{
    GHashTableIter iter;
    gpointer key, value;

    if (!watcher) return;
    if (--watcher->refcount > 0) return;

    g_hash_table_iter_init (&iter, watcher->sn_hosts_id);
    while (g_hash_table_iter_next (&iter, &key, &value)) g_bus_unwatch_name (GPOINTER_TO_UINT (value));

    g_hash_table_iter_init (&iter, watcher->sn_items_id);
    while (g_hash_table_iter_next (&iter, &key, &value)) g_bus_unwatch_name (GPOINTER_TO_UINT (value));

    if (watcher->dbus_object_id)
        g_dbus_connection_unregister_object (watcher->watcher_connection, watcher->dbus_object_id);
    if (watcher->dbus_name_id)
        g_bus_unown_name (watcher->dbus_name_id);

    g_hash_table_destroy (watcher->sn_hosts_id);
    g_hash_table_destroy (watcher->sn_items_id);

    if (g_watcher_instance == watcher) g_watcher_instance = NULL;

    g_free (watcher);
}

static void on_bus_acquired (GDBusConnection *connection, const gchar *name, gpointer userdata)
{
    Watcher *w = (Watcher *) userdata;

    w->dbus_object_id = g_dbus_connection_register_object (connection, SNW_PATH, get_introspection_data (),
        &w->interface_table, w, NULL, NULL);
    w->watcher_connection = connection;
}

static void item_watch_ctx_free (gpointer data)
{
    gpointer *ctx = (gpointer *) data;

    g_free (ctx[1]);
    g_free (ctx);
}

static void on_item_name_vanished (GDBusConnection *connection, const gchar *name, gpointer userdata)
{
    gpointer *ctx = (gpointer *) userdata;
    Watcher *w = ctx[0];
    const char *full_obj_path = (const char *) ctx[1];
    guint watch_id;

    watch_id = GPOINTER_TO_UINT (g_hash_table_lookup (w->sn_items_id, full_obj_path));
    g_bus_unwatch_name (watch_id);
    g_hash_table_remove (w->sn_items_id, full_obj_path);
    emit_signal_s (w, "StatusNotifierItemUnregistered", full_obj_path);
}

static void register_status_notifier_item (Watcher *w, GDBusConnection *connection, const char *sender, const char *path)
{
    char *full_obj_path = g_strconcat (sender, path, NULL);
    gpointer *ctx;
    guint watch_id;

    emit_signal_s (w, "StatusNotifierItemRegistered", full_obj_path);

    ctx = g_new0 (gpointer, 2);
    ctx[0] = w;
    ctx[1] = g_strdup (full_obj_path);
    watch_id = g_bus_watch_name (G_BUS_TYPE_SESSION, sender, G_BUS_NAME_WATCHER_FLAGS_NONE,
        NULL, on_item_name_vanished, ctx, item_watch_ctx_free);

    g_hash_table_insert (w->sn_items_id, full_obj_path, GUINT_TO_POINTER (watch_id));
}

static void on_host_name_appeared (GDBusConnection *connection, const gchar *name, const gchar *name_owner, gpointer userdata)
{
    gpointer *ctx = (gpointer *) userdata;
    Watcher *w = ctx[0];
    gboolean is_host_registred_changed = GPOINTER_TO_INT (ctx[1]);

    emit_signal0 (w, "StatusNotifierHostRegistered");
    if (is_host_registred_changed)
    {
        GVariantBuilder props_builder, inval_builder;

        g_variant_builder_init (&props_builder, G_VARIANT_TYPE ("a{sv}"));
        g_variant_builder_add (&props_builder, "{sv}", "IsStatusNotifierHostRegistered", g_variant_new_boolean (TRUE));

        g_variant_builder_init (&inval_builder, G_VARIANT_TYPE ("as"));

        g_dbus_connection_emit_signal (w->watcher_connection, NULL, SNW_PATH, "org.freedesktop.DBus.Properties", "PropertiesChanged",
            g_variant_new ("(sa{sv}as)", SNW_IFACE, &props_builder, &inval_builder), NULL);
    }
}

static void on_host_name_vanished (GDBusConnection *connection, const gchar *name, gpointer userdata)
{
    gpointer *ctx = (gpointer *) userdata;
    Watcher *w = ctx[0];
    guint watch_id;

    watch_id = GPOINTER_TO_UINT (g_hash_table_lookup (w->sn_hosts_id, name));
    g_bus_unwatch_name (watch_id);
    g_hash_table_remove (w->sn_hosts_id, name);
}

static void register_status_notifier_host (Watcher *w, GDBusConnection *connection, const char *service)
{
    gpointer *ctx = g_new0 (gpointer, 2);
    guint watch_id;

    ctx[0] = w;
    ctx[1] = GINT_TO_POINTER (g_hash_table_size (w->sn_hosts_id) == 0);

    watch_id = g_bus_watch_name_on_connection (connection, service, G_BUS_NAME_WATCHER_FLAGS_NONE,
        on_host_name_appeared, on_host_name_vanished, ctx, g_free);

    g_hash_table_insert (w->sn_hosts_id, g_strdup (service), GUINT_TO_POINTER (watch_id));
}

static void on_interface_method_call (GDBusConnection *connection, const gchar *sender, const gchar *object_path,
    const gchar *interface_name, const gchar *method_name, GVariant *parameters,
    GDBusMethodInvocation *invocation, gpointer userdata)
{
    Watcher *w = (Watcher *) userdata;
    const gchar *service;

    if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(s)")))
    {
        fprintf (stderr, "StatusNotifierWatcher: invalid argument type: expected (s), got %s\n",
            g_variant_get_type_string (parameters));
        return;
    }

    g_variant_get (parameters, "(&s)", &service);

    if (!g_strcmp0 (method_name, "RegisterStatusNotifierItem"))
    {
        register_status_notifier_item (w, connection, service[0] == '/' ? sender : service,
            service[0] == '/' ? service : "/StatusNotifierItem");
    }
    else if (!g_strcmp0 (method_name, "RegisterStatusNotifierHost"))
    {
        register_status_notifier_host (w, connection, service);
    }
    else
    {
        fprintf (stderr, "StatusNotifierWatcher: unknown method %s\n", method_name);
        return;
    }

    g_dbus_method_invocation_return_value (invocation, NULL);
}

static GVariant *get_registred_items (Watcher *w)
{
    GVariantBuilder builder;
    GHashTableIter iter;
    gpointer key, value;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("as"));
    g_hash_table_iter_init (&iter, w->sn_items_id);
    while (g_hash_table_iter_next (&iter, &key, &value))
        g_variant_builder_add (&builder, "s", (const char *) key);

    return g_variant_builder_end (&builder);
}

static GVariant *on_interface_get_property (GDBusConnection *connection, const gchar *sender, const gchar *object_path,
    const gchar *interface_name, const gchar *property_name, GError **error, gpointer userdata)
{
    Watcher *w = (Watcher *) userdata;

    if (!g_strcmp0 (property_name, "RegisteredStatusNotifierItems"))
    {
        return get_registred_items (w);
    }
    else if (!g_strcmp0 (property_name, "IsStatusNotifierHostRegistered"))
    {
        return g_variant_new_boolean (g_hash_table_size (w->sn_hosts_id) != 0);
    }
    else if (!g_strcmp0 (property_name, "ProtocolVersion"))
    {
        return g_variant_new_int32 (0);
    }
    else
    {
        fprintf (stderr, "StatusNotifierWatcher: Unknown property %s\n", property_name);
    }

    return NULL;
}

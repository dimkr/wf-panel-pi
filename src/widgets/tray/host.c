#include "host.h"
#include "tray.h"
#include "watcher.h"

#include <unistd.h>

typedef struct
{
    StatusNotifierHost *host;
    gboolean *destroyed;
} HostCtx;

static int hosts_counter;

/*----------------------------------------------------------------------------*/
/* Prototypes                                                                 */
/*----------------------------------------------------------------------------*/

static HostCtx *host_ctx_new (StatusNotifierHost *host);
static void host_ctx_free (gpointer data);
static void host_ctx_free_closure (gpointer data, GClosure *closure);
static void on_bus_acquired (GDBusConnection *connection, const gchar *name, gpointer userdata);
static void on_watcher_proxy_ready (GObject *source, GAsyncResult *res, gpointer userdata);
static void on_watcher_g_signal (GDBusProxy *proxy, const gchar *sender_name, const gchar *signal_name,
    GVariant *parameters, gpointer userdata);
static void on_watcher_appeared (GDBusConnection *connection, const gchar *name, const gchar *name_owner, gpointer userdata);
static void on_watcher_vanished (GDBusConnection *connection, const gchar *name, gpointer userdata);

static HostCtx *host_ctx_new (StatusNotifierHost *host)
{
    HostCtx *ctx = g_new0 (HostCtx, 1);

    ctx->host = host;
    ctx->destroyed = g_rc_box_acquire (host->destroyed);

    return ctx;
}

static void host_ctx_free (gpointer data)
{
    HostCtx *ctx = (HostCtx *) data;

    g_rc_box_release (ctx->destroyed);
    g_free (ctx);
}

static void host_ctx_free_closure (gpointer data, GClosure *closure)
{
    host_ctx_free (data);
}

StatusNotifierHost *status_notifier_host_new (WidgetStatusNotifier *tray)
{
    StatusNotifierHost *host = g_new0 (StatusNotifierHost, 1);
    char *name;

    host->watcher_ptr = watcher_launch ();
    host->destroyed = g_rc_box_new0 (gboolean);
    host->tray = tray;

    name = g_strdup_printf ("org.kde.StatusNotifierHost-%d-%d", getpid (), ++hosts_counter);
    host->dbus_name_id = g_bus_own_name (G_BUS_TYPE_SESSION, name, G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired, NULL, NULL, host, NULL);
    g_free (name);

    return host;
}

static void on_bus_acquired (GDBusConnection *connection, const gchar *name, gpointer userdata)
{
    StatusNotifierHost *host = (StatusNotifierHost *) userdata;

    host->watcher_id = g_bus_watch_name_on_connection (connection, SNW_NAME, G_BUS_NAME_WATCHER_FLAGS_NONE,
        on_watcher_appeared, on_watcher_vanished, host_ctx_new (host), host_ctx_free);
}

static void on_watcher_appeared (GDBusConnection *connection, const gchar *name, const gchar *name_owner, gpointer userdata)
{
    HostCtx *ctx = (HostCtx *) userdata;

    if (*ctx->destroyed) return;

    g_dbus_proxy_new (connection, G_DBUS_PROXY_FLAGS_NONE, NULL, SNW_NAME, SNW_PATH, SNW_IFACE, NULL,
        on_watcher_proxy_ready, host_ctx_new (ctx->host));
}

static void on_watcher_proxy_ready (GObject *source, GAsyncResult *res, gpointer userdata)
{
    HostCtx *ctx = (HostCtx *) userdata;
    GError *error = NULL;
    GVariant *registered_items_var;

    if (*ctx->destroyed)
    {
        host_ctx_free (ctx);
        return;
    }

    ctx->host->watcher_proxy = g_dbus_proxy_new_finish (res, &error);
    if (!ctx->host->watcher_proxy)
    {
        g_clear_error (&error);
        host_ctx_free (ctx);
        return;
    }

    g_dbus_proxy_call (ctx->host->watcher_proxy, "RegisterStatusNotifierHost", g_variant_new ("(s)", SNW_NAME),
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
    g_signal_connect_data (ctx->host->watcher_proxy, "g-signal", G_CALLBACK (on_watcher_g_signal),
        host_ctx_new (ctx->host), host_ctx_free_closure, (GConnectFlags) 0);

    registered_items_var = g_dbus_proxy_get_cached_property (ctx->host->watcher_proxy, "RegisteredStatusNotifierItems");
    if (registered_items_var)
    {
        GVariantIter iter;
        const gchar *service;

        g_variant_iter_init (&iter, registered_items_var);
        while (g_variant_iter_next (&iter, "&s", &service))
            widget_status_notifier_add_item (ctx->host->tray, service);

        g_variant_unref (registered_items_var);
    }

    host_ctx_free (ctx);
}

static void on_watcher_g_signal (GDBusProxy *proxy, const gchar *sender_name, const gchar *signal_name,
    GVariant *parameters, gpointer userdata)
{
    HostCtx *ctx = (HostCtx *) userdata;
    const gchar *item_path;

    if (*ctx->destroyed) return;

    if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(s)"))) return;

    g_variant_get (parameters, "(&s)", &item_path);
    if (!g_strcmp0 (signal_name, "StatusNotifierItemRegistered"))
    {
        widget_status_notifier_add_item (ctx->host->tray, item_path);
    }
    else if (!g_strcmp0 (signal_name, "StatusNotifierItemUnregistered"))
    {
        widget_status_notifier_remove_item (ctx->host->tray, item_path);
    }
}

static void on_watcher_vanished (GDBusConnection *connection, const gchar *name, gpointer userdata)
{
    HostCtx *ctx = (HostCtx *) userdata;

    if (*ctx->destroyed) return;

    g_bus_unwatch_name (ctx->host->watcher_id);
}

void status_notifier_host_free (StatusNotifierHost *host)
{
    *host->destroyed = TRUE;
    g_bus_unwatch_name (host->watcher_id);
    g_bus_unown_name (host->dbus_name_id);

    if (host->watcher_proxy) g_object_unref (host->watcher_proxy);
    watcher_unref (host->watcher_ptr);
    g_rc_box_release (host->destroyed);

    g_free (host);
}

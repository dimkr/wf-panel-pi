/*============================================================================
Copyright (c) 2020-2025 Raspberry Pi
All rights reserved.

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

#include "plugin.h"

#include "volume.h"
#include "commongui.h"
#include "bluetooth.h"

#include "pipewire.h"

#include <pipewire/extensions/metadata.h>
#include <spa/param/props.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>
#include <spa/pod/parser.h>
#include <spa/utils/json.h>

/*----------------------------------------------------------------------------*/
/* Typedefs and macros                                                        */
/*----------------------------------------------------------------------------*/

/*
 * Access to the controller is via asynchronous PipeWire events which arrive
 * on the thread owning vol->pw_loop. Because the plugin itself runs
 * synchronously, all controller access functions below are wrapped in code
 * which performs a "roundtrip": it asks the core for a sync point and waits
 * until the matching "done" event has been dispatched, guaranteeing that any
 * registry / node / device / metadata events triggered by the call have
 * already run their callbacks.
 */

#define MAX_CHANNELS 8

typedef struct
{
    uint32_t id;
    char *name;             /* node.name */
    char *description;      /* node.description */
    char *media_class;      /* media.class */
    int device_id;          /* owning device id, or -1 */
    struct pw_proxy *proxy;
    struct spa_hook proxy_listener;
    float volumes[MAX_CHANNELS];
    int n_channels;
    gboolean mute;
} PwNodeInfo;

typedef struct
{
    uint32_t id;
    char *name;              /* device.name */
    char *description;       /* device.description */
    char *api;                /* device.api */
    char *form_factor;        /* device.form-factor */
    struct pw_proxy *proxy;
    struct spa_hook proxy_listener;
    char *active_profile_name;
    char *active_profile_description;
    GList *profiles;          /* list of PwProfileInfo * */
} PwDeviceInfo;

typedef struct
{
    char *name;
    char *description;
} PwProfileInfo;

/*----------------------------------------------------------------------------*/
/* Prototypes                                                                 */
/*----------------------------------------------------------------------------*/

static void pw_roundtrip (VolumePlugin *vol);
static void pw_error_handler (VolumePlugin *vol, char *name);
static void pw_node_hash_free (gpointer data);
static void pw_device_hash_free (gpointer data);
static void pw_profile_info_free (gpointer data);

static void on_core_done (void *data, uint32_t id, int seq);
static void on_core_error (void *data, uint32_t id, int seq, int res, const char *message);

static void on_node_info (void *data, const struct pw_node_info *info);
static void on_node_param (void *data, int seq, uint32_t id, uint32_t index, uint32_t next, const struct spa_pod *param);
static void on_device_info (void *data, const struct pw_device_info *info);
static void on_device_param (void *data, int seq, uint32_t id, uint32_t index, uint32_t next, const struct spa_pod *param);

static void on_registry_global (void *data, uint32_t id, uint32_t permissions, const char *type, uint32_t version, const struct spa_dict *props);
static void on_registry_global_remove (void *data, uint32_t id);

static void on_default_metadata_property (VolumePlugin *vol, uint32_t subject, const char *key, const char *type, const char *value);
static int on_metadata_property (void *data, uint32_t subject, const char *key, const char *type, const char *value);

static PwNodeInfo *pw_find_node_by_name (VolumePlugin *vol, const char *name);
static PwNodeInfo *pw_find_default_node (VolumePlugin *vol, gboolean input_control);
static PwDeviceInfo *pw_find_device_by_name (VolumePlugin *vol, const char *name);
static gboolean pw_device_has_direction (VolumePlugin *vol, PwDeviceInfo *dev, gboolean input_control);

static int pw_query_node_props (VolumePlugin *vol, PwNodeInfo *node);
static int pw_set_node_props (VolumePlugin *vol, PwNodeInfo *node, const float *volumes, int n_channels, int mute, gboolean set_volume, gboolean set_mute);

static int pw_set_default (VolumePlugin *vol, gboolean input_control, const char *name);
static void pw_move_streams (VolumePlugin *vol, gboolean input_control);

/*----------------------------------------------------------------------------*/
/* Function definitions                                                       */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* PipeWire controller initialisation / teardown                              */
/*----------------------------------------------------------------------------*/

static const struct pw_core_events core_events = {
    PW_VERSION_CORE_EVENTS,
    .done = on_core_done,
    .error = on_core_error,
};

static const struct pw_registry_events registry_events = {
    PW_VERSION_REGISTRY_EVENTS,
    .global = on_registry_global,
    .global_remove = on_registry_global_remove,
};

static const struct pw_node_events node_events = {
    PW_VERSION_NODE_EVENTS,
    .info = on_node_info,
    .param = on_node_param,
};

static const struct pw_device_events device_events = {
    PW_VERSION_DEVICE_EVENTS,
    .info = on_device_info,
    .param = on_device_param,
};

static const struct pw_metadata_events metadata_events = {
    PW_VERSION_METADATA_EVENTS,
    .property = on_metadata_property,
};

void pw_backend_init (VolumePlugin *vol)
{
    DEBUG ("pw_backend_init");

    pw_init (NULL, NULL);

    vol->pw_core = NULL;
    vol->pw_idle_timer = 0;
    vol->pw_sync_seq = 0;
    vol->pw_sync_done = FALSE;

    vol->pw_node_info = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, pw_node_hash_free);
    vol->pw_device_info = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, pw_device_hash_free);

    vol->pw_loop = pw_thread_loop_new ("volume-plugin", NULL);
    if (!vol->pw_loop)
    {
        pw_error_handler (vol, "create thread loop");
        return;
    }

    if (pw_thread_loop_start (vol->pw_loop) < 0)
    {
        pw_error_handler (vol, "start thread loop");
        return;
    }

    pw_thread_loop_lock (vol->pw_loop);

    vol->pw_context = pw_context_new (pw_thread_loop_get_loop (vol->pw_loop), NULL, 0);
    if (!vol->pw_context)
    {
        pw_thread_loop_unlock (vol->pw_loop);
        pw_error_handler (vol, "create context");
        return;
    }

    vol->pw_core = pw_context_connect (vol->pw_context, NULL, 0);
    if (!vol->pw_core)
    {
        pw_thread_loop_unlock (vol->pw_loop);
        pw_error_handler (vol, "connect context");
        return;
    }

    pw_core_add_listener (vol->pw_core, &vol->pw_core_listener, &core_events, vol);

    vol->pw_registry = pw_core_get_registry (vol->pw_core, PW_VERSION_REGISTRY, 0);
    pw_registry_add_listener (vol->pw_registry, &vol->pw_registry_listener, &registry_events, vol);

    pw_thread_loop_unlock (vol->pw_loop);

    /* wait for the registry to be populated with the initial set of globals */
    pw_roundtrip (vol);

    vol->pw_default_sink = NULL;
    vol->pw_default_source = NULL;
    vol->pw_profile = NULL;
    vol->pw_indices = NULL;

    pw_get_default_sink_source (vol);
    pw_move_output_streams (vol);
    pw_move_input_streams (vol);
}

/* Teardown PipeWire controller */

void pw_backend_terminate (VolumePlugin *vol)
{
    if (vol->pw_loop != NULL)
    {
        pw_thread_loop_lock (vol->pw_loop);

        if (vol->pw_node_info) g_hash_table_remove_all (vol->pw_node_info);
        if (vol->pw_device_info) g_hash_table_remove_all (vol->pw_device_info);

        if (vol->pw_default_metadata)
        {
            spa_hook_remove (&vol->pw_default_metadata_listener);
            pw_proxy_destroy ((struct pw_proxy *) vol->pw_default_metadata);
            vol->pw_default_metadata = NULL;
        }

        if (vol->pw_registry)
        {
            spa_hook_remove (&vol->pw_registry_listener);
            pw_proxy_destroy ((struct pw_proxy *) vol->pw_registry);
            vol->pw_registry = NULL;
        }

        if (vol->pw_core)
        {
            spa_hook_remove (&vol->pw_core_listener);
            pw_core_disconnect (vol->pw_core);
            vol->pw_core = NULL;
        }

        if (vol->pw_context)
        {
            pw_context_destroy (vol->pw_context);
            vol->pw_context = NULL;
        }

        pw_thread_loop_unlock (vol->pw_loop);

        pw_thread_loop_stop (vol->pw_loop);
        pw_thread_loop_destroy (vol->pw_loop);
        vol->pw_loop = NULL;
    }

    if (vol->pw_node_info)
    {
        g_hash_table_destroy (vol->pw_node_info);
        vol->pw_node_info = NULL;
    }
    if (vol->pw_device_info)
    {
        g_hash_table_destroy (vol->pw_device_info);
        vol->pw_device_info = NULL;
    }

    if (vol->pw_idle_timer) g_source_remove (vol->pw_idle_timer);
    vol->pw_idle_timer = 0;
}

/* Handler for unrecoverable errors - terminates the controller */

static void pw_error_handler (VolumePlugin *vol, char *name)
{
    g_warning ("%s: pipewire controller error\n", name);
    pw_backend_terminate (vol);
}

static void pw_node_hash_free (gpointer data)
{
    PwNodeInfo *node = (PwNodeInfo *) data;

    if (node->proxy)
    {
        spa_hook_remove (&node->proxy_listener);
        pw_proxy_destroy (node->proxy);
    }
    g_free (node->name);
    g_free (node->description);
    g_free (node->media_class);
    g_free (node);
}

static void pw_profile_info_free (gpointer data)
{
    PwProfileInfo *prof = (PwProfileInfo *) data;

    g_free (prof->name);
    g_free (prof->description);
    g_free (prof);
}

static void pw_device_hash_free (gpointer data)
{
    PwDeviceInfo *dev = (PwDeviceInfo *) data;

    if (dev->proxy)
    {
        spa_hook_remove (&dev->proxy_listener);
        pw_proxy_destroy (dev->proxy);
    }
    g_free (dev->name);
    g_free (dev->description);
    g_free (dev->api);
    g_free (dev->form_factor);
    g_free (dev->active_profile_name);
    g_free (dev->active_profile_description);
    g_list_free_full (dev->profiles, pw_profile_info_free);
    g_free (dev);
}

/*----------------------------------------------------------------------------*/
/* Core synchronisation                                                       */
/*----------------------------------------------------------------------------*/

static void on_core_done (void *data, uint32_t id, int seq)
{
    VolumePlugin *vol = (VolumePlugin *) data;

    if (id == PW_ID_CORE && seq == vol->pw_sync_seq)
    {
        vol->pw_sync_done = TRUE;
        pw_thread_loop_signal (vol->pw_loop, FALSE);
    }
}

static void on_core_error (void *data, uint32_t id, int seq, int res, const char *message)
{
    VolumePlugin *vol = (VolumePlugin *) data;

    DEBUG ("pipewire core error id:%u seq:%d res:%d : %s", id, seq, res, message);
    if (vol->pw_error_msg) g_free (vol->pw_error_msg);
    vol->pw_error_msg = g_strdup (message);

    pw_thread_loop_signal (vol->pw_loop, FALSE);
}

/* Perform a core sync roundtrip - blocks until all events already queued by the server have been dispatched */

static void pw_roundtrip (VolumePlugin *vol)
{
    if (!vol->pw_core || !vol->pw_loop) return;

    pw_thread_loop_lock (vol->pw_loop);

    if (vol->pw_error_msg)
    {
        g_free (vol->pw_error_msg);
        vol->pw_error_msg = NULL;
    }

    vol->pw_sync_done = FALSE;
    vol->pw_sync_seq = pw_core_sync (vol->pw_core, PW_ID_CORE, vol->pw_sync_seq + 1);

    while (!vol->pw_sync_done && !vol->pw_error_msg)
    {
        pw_thread_loop_wait (vol->pw_loop);
    }

    pw_thread_loop_unlock (vol->pw_loop);
}

/*----------------------------------------------------------------------------*/
/* Registry - node and device discovery                                       */
/*----------------------------------------------------------------------------*/

static gboolean pw_update_disp_cb (gpointer userdata)
{
    VolumePlugin *vol = (VolumePlugin *) userdata;

    vol->pw_idle_timer = 0;
    volume_update_display (vol);
    return FALSE;
}

static void schedule_display_update (VolumePlugin *vol)
{
    if (vol->pw_idle_timer == 0)
        vol->pw_idle_timer = g_idle_add (pw_update_disp_cb, vol);
}

static void on_node_info (void *data, const struct pw_node_info *info)
{
    PwNodeInfo *node = (PwNodeInfo *) data;

    if (info->props)
    {
        const char *desc = spa_dict_lookup (info->props, PW_KEY_NODE_DESCRIPTION);
        if (desc)
        {
            g_free (node->description);
            node->description = g_strdup (desc);
        }
    }
}

static void on_node_param (void *data, int seq, uint32_t id, uint32_t index, uint32_t next, const struct spa_pod *param)
{
    PwNodeInfo *node = (PwNodeInfo *) data;

    if (id != SPA_PARAM_Props || param == NULL) return;

    const struct spa_pod_prop *prop;
    const struct spa_pod_object *obj = (const struct spa_pod_object *) param;

    SPA_POD_OBJECT_FOREACH (obj, prop)
    {
        switch (prop->key)
        {
            case SPA_PROP_mute:
            {
                bool m;
                if (spa_pod_get_bool (&prop->value, &m) == 0) node->mute = m ? TRUE : FALSE;
                break;
            }
            case SPA_PROP_channelVolumes:
            {
                float vals[MAX_CHANNELS];
                uint32_t n = spa_pod_copy_array (&prop->value, SPA_TYPE_Float, vals, MAX_CHANNELS);
                if (n > 0)
                {
                    node->n_channels = (int) n;
                    memcpy (node->volumes, vals, n * sizeof (float));
                }
                break;
            }
            default:
                break;
        }
    }
}

static void on_device_info (void *data, const struct pw_device_info *info)
{
    PwDeviceInfo *dev = (PwDeviceInfo *) data;

    if (info->props)
    {
        const char *desc = spa_dict_lookup (info->props, PW_KEY_DEVICE_DESCRIPTION);
        if (desc)
        {
            g_free (dev->description);
            dev->description = g_strdup (desc);
        }
    }
}

static void on_device_param (void *data, int seq, uint32_t id, uint32_t index, uint32_t next, const struct spa_pod *param)
{
    PwDeviceInfo *dev = (PwDeviceInfo *) data;

    if (param == NULL) return;

    if (id == SPA_PARAM_EnumProfile)
    {
        int32_t idx = -1;
        const char *name = NULL, *description = NULL;

        if (spa_pod_parse_object (param, SPA_TYPE_OBJECT_ParamProfile, NULL,
                SPA_PARAM_PROFILE_index, SPA_POD_Int (&idx),
                SPA_PARAM_PROFILE_name, SPA_POD_OPT_String (&name),
                SPA_PARAM_PROFILE_description, SPA_POD_OPT_String (&description)) >= 0)
        {
            if (name)
            {
                PwProfileInfo *prof = g_new0 (PwProfileInfo, 1);
                prof->name = g_strdup (name);
                prof->description = g_strdup (description ? description : name);
                dev->profiles = g_list_append (dev->profiles, prof);
            }
        }
    }
    else if (id == SPA_PARAM_Profile)
    {
        int32_t idx = -1;
        const char *name = NULL, *description = NULL;

        if (spa_pod_parse_object (param, SPA_TYPE_OBJECT_ParamProfile, NULL,
                SPA_PARAM_PROFILE_index, SPA_POD_Int (&idx),
                SPA_PARAM_PROFILE_name, SPA_POD_OPT_String (&name),
                SPA_PARAM_PROFILE_description, SPA_POD_OPT_String (&description)) >= 0)
        {
            if (name)
            {
                g_free (dev->active_profile_name);
                dev->active_profile_name = g_strdup (name);
                g_free (dev->active_profile_description);
                dev->active_profile_description = g_strdup (description ? description : name);
            }
        }
    }
}

static void on_registry_global (void *data, uint32_t id, uint32_t permissions, const char *type, uint32_t version, const struct spa_dict *props)
{
    VolumePlugin *vol = (VolumePlugin *) data;

    if (!props) return;

    if (!g_strcmp0 (type, PW_TYPE_INTERFACE_Node))
    {
        const char *media_class = spa_dict_lookup (props, PW_KEY_MEDIA_CLASS);
        if (!media_class) return;

        if (g_strcmp0 (media_class, "Audio/Sink") && g_strcmp0 (media_class, "Audio/Source")
            && g_strcmp0 (media_class, "Stream/Output/Audio") && g_strcmp0 (media_class, "Stream/Input/Audio"))
            return;

        PwNodeInfo *node = g_new0 (PwNodeInfo, 1);
        node->id = id;
        node->media_class = g_strdup (media_class);
        node->name = g_strdup (spa_dict_lookup (props, PW_KEY_NODE_NAME));
        node->description = g_strdup (spa_dict_lookup (props, PW_KEY_NODE_DESCRIPTION));
        const char *devid = spa_dict_lookup (props, PW_KEY_DEVICE_ID);
        node->device_id = devid ? atoi (devid) : -1;

        node->proxy = pw_registry_bind (vol->pw_registry, id, type, PW_VERSION_NODE, 0);
        if (node->proxy)
        {
            pw_proxy_add_object_listener (node->proxy, &node->proxy_listener, &node_events, node);
            uint32_t ids[1] = { SPA_PARAM_Props };
            pw_node_subscribe_params ((struct pw_node *) node->proxy, ids, 1);
            pw_node_enum_params ((struct pw_node *) node->proxy, 0, SPA_PARAM_Props, 0, 1, NULL);
        }

        g_hash_table_insert (vol->pw_node_info, GUINT_TO_POINTER (id), node);
        schedule_display_update (vol);
        return;
    }

    if (!g_strcmp0 (type, PW_TYPE_INTERFACE_Device))
    {
        const char *media_class = spa_dict_lookup (props, PW_KEY_MEDIA_CLASS);
        if (g_strcmp0 (media_class, "Audio/Device")) return;

        PwDeviceInfo *dev = g_new0 (PwDeviceInfo, 1);
        dev->id = id;
        dev->name = g_strdup (spa_dict_lookup (props, PW_KEY_DEVICE_NAME));
        dev->description = g_strdup (spa_dict_lookup (props, PW_KEY_DEVICE_DESCRIPTION));
        dev->api = g_strdup (spa_dict_lookup (props, PW_KEY_DEVICE_API));
        dev->form_factor = g_strdup (spa_dict_lookup (props, PW_KEY_DEVICE_FORM_FACTOR));

        dev->proxy = pw_registry_bind (vol->pw_registry, id, type, PW_VERSION_DEVICE, 0);
        if (dev->proxy)
        {
            pw_proxy_add_object_listener (dev->proxy, &dev->proxy_listener, &device_events, dev);
            uint32_t ids[2] = { SPA_PARAM_EnumProfile, SPA_PARAM_Profile };
            pw_device_subscribe_params ((struct pw_device *) dev->proxy, ids, 2);
            pw_device_enum_params ((struct pw_device *) dev->proxy, 0, SPA_PARAM_EnumProfile, 0, UINT32_MAX, NULL);
            pw_device_enum_params ((struct pw_device *) dev->proxy, 0, SPA_PARAM_Profile, 0, 1, NULL);
        }

        g_hash_table_insert (vol->pw_device_info, GUINT_TO_POINTER (id), dev);
        schedule_display_update (vol);
        return;
    }

    if (!g_strcmp0 (type, PW_TYPE_INTERFACE_Metadata))
    {
        const char *name = spa_dict_lookup (props, PW_KEY_METADATA_NAME);
        if (g_strcmp0 (name, "default")) return;

        if (vol->pw_default_metadata) return;

        vol->pw_default_metadata = (struct pw_metadata *) pw_registry_bind (vol->pw_registry, id, type, PW_VERSION_METADATA, 0);
        if (vol->pw_default_metadata)
            pw_metadata_add_listener (vol->pw_default_metadata, &vol->pw_default_metadata_listener, &metadata_events, vol);
        return;
    }
}

static void on_registry_global_remove (void *data, uint32_t id)
{
    VolumePlugin *vol = (VolumePlugin *) data;

    g_hash_table_remove (vol->pw_node_info, GUINT_TO_POINTER (id));
    g_hash_table_remove (vol->pw_device_info, GUINT_TO_POINTER (id));

    schedule_display_update (vol);
}

/*----------------------------------------------------------------------------*/
/* Default sink / source metadata                                             */
/*----------------------------------------------------------------------------*/

static void on_default_metadata_property (VolumePlugin *vol, uint32_t subject, const char *key, const char *type, const char *value)
{
    if (subject != PW_ID_CORE || !key) return;

    gboolean sink = !g_strcmp0 (key, "default.audio.sink");
    gboolean source = !g_strcmp0 (key, "default.audio.source");
    if (!sink && !source) return;

    char *name = NULL;
    if (value)
    {
        struct spa_json it[2];
        char json_key[256], json_val[256];

        spa_json_init (&it[0], value, strlen (value));
        if (spa_json_enter_object (&it[0], &it[1]) > 0)
        {
            while (spa_json_get_string (&it[1], json_key, sizeof (json_key)) > 0)
            {
                if (!strcmp (json_key, "name"))
                {
                    if (spa_json_get_string (&it[1], json_val, sizeof (json_val)) > 0)
                        name = g_strdup (json_val);
                }
                else spa_json_next (&it[1], NULL);
            }
        }
    }

    if (sink)
    {
        g_free (vol->pw_default_sink);
        vol->pw_default_sink = name;
    }
    else
    {
        g_free (vol->pw_default_source);
        vol->pw_default_source = name;
    }
}

static int on_metadata_property (void *data, uint32_t subject, const char *key, const char *type, const char *value)
{
    VolumePlugin *vol = (VolumePlugin *) data;

    on_default_metadata_property (vol, subject, key, type, value);
    schedule_display_update (vol);
    return 0;
}

/*----------------------------------------------------------------------------*/
/* Lookup helpers                                                             */
/*----------------------------------------------------------------------------*/

static PwNodeInfo *pw_find_node_by_name (VolumePlugin *vol, const char *name)
{
    GHashTableIter iter;
    gpointer key, value;

    if (!name) return NULL;

    g_hash_table_iter_init (&iter, vol->pw_node_info);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        PwNodeInfo *node = (PwNodeInfo *) value;
        if (!g_strcmp0 (node->name, name)) return node;
    }
    return NULL;
}

static PwNodeInfo *pw_find_default_node (VolumePlugin *vol, gboolean input_control)
{
    return pw_find_node_by_name (vol, input_control ? vol->pw_default_source : vol->pw_default_sink);
}

static PwDeviceInfo *pw_find_device_by_name (VolumePlugin *vol, const char *name)
{
    GHashTableIter iter;
    gpointer key, value;

    if (!name) return NULL;

    g_hash_table_iter_init (&iter, vol->pw_device_info);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        PwDeviceInfo *dev = (PwDeviceInfo *) value;
        if (!g_strcmp0 (dev->name, name)) return dev;
    }
    return NULL;
}

/* A device "has" a direction if one of its child nodes is a sink (output) or source (input) */

static gboolean pw_device_has_direction (VolumePlugin *vol, PwDeviceInfo *dev, gboolean input_control)
{
    GHashTableIter iter;
    gpointer key, value;
    const char *want = input_control ? "Audio/Source" : "Audio/Sink";

    g_hash_table_iter_init (&iter, vol->pw_node_info);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        PwNodeInfo *node = (PwNodeInfo *) value;
        if (node->device_id == (int) dev->id && !g_strcmp0 (node->media_class, want)) return TRUE;
    }
    return FALSE;
}

/*----------------------------------------------------------------------------*/
/* Volume and mute control                                                    */
/*----------------------------------------------------------------------------*/

static int pw_query_node_props (VolumePlugin *vol, PwNodeInfo *node)
{
    if (!node->proxy) return 0;

    pw_thread_loop_lock (vol->pw_loop);
    pw_node_enum_params ((struct pw_node *) node->proxy, 0, SPA_PARAM_Props, 0, 1, NULL);
    pw_thread_loop_unlock (vol->pw_loop);

    pw_roundtrip (vol);
    return vol->pw_error_msg ? 0 : 1;
}

int pw_get_volume (VolumePlugin *vol, gboolean input_control)
{
    PwNodeInfo *node = pw_find_default_node (vol, input_control);
    if (!node) return 0;

    pw_query_node_props (vol, node);

    vol->pw_channels = node->n_channels;
    vol->pw_mute = node->mute;
    vol->pw_volume = node->n_channels > 0 ? (int) (node->volumes[0] * 100.0f + 0.5f) : 0;

    return vol->pw_volume;
}

static int pw_set_node_props (VolumePlugin *vol, PwNodeInfo *node, const float *volumes, int n_channels, int mute, gboolean set_volume, gboolean set_mute)
{
    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT (buffer, sizeof (buffer));
    struct spa_pod_frame f;
    struct spa_pod *param;

    if (!node || !node->proxy) return 0;

    if (vol->pw_error_msg)
    {
        g_free (vol->pw_error_msg);
        vol->pw_error_msg = NULL;
    }

    spa_pod_builder_push_object (&b, &f, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
    if (set_mute)
    {
        spa_pod_builder_prop (&b, SPA_PROP_mute, 0);
        spa_pod_builder_bool (&b, mute ? true : false);
    }
    if (set_volume)
    {
        spa_pod_builder_prop (&b, SPA_PROP_channelVolumes, 0);
        spa_pod_builder_array (&b, sizeof (float), SPA_TYPE_Float, n_channels, volumes);
    }
    param = (struct spa_pod *) spa_pod_builder_pop (&b, &f);

    pw_thread_loop_lock (vol->pw_loop);
    pw_node_set_param ((struct pw_node *) node->proxy, SPA_PARAM_Props, 0, param);
    pw_thread_loop_unlock (vol->pw_loop);

    pw_roundtrip (vol);

    return vol->pw_error_msg ? 0 : 1;
}

int pw_set_volume (VolumePlugin *vol, int volume, gboolean input_control)
{
    PwNodeInfo *node = pw_find_default_node (vol, input_control);
    float volumes[MAX_CHANNELS];
    int i, n;
    float lin;

    DEBUG ("pw_set_volume %d %d", volume, input_control);

    if (!node) return 0;

    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    vol->pw_volume = volume;
    lin = volume / 100.0f;

    n = node->n_channels > 0 ? node->n_channels : 2;
    for (i = 0; i < n && i < MAX_CHANNELS; i++) volumes[i] = lin;

    return pw_set_node_props (vol, node, volumes, n, 0, TRUE, FALSE);
}

int pw_get_mute (VolumePlugin *vol, gboolean input_control)
{
    PwNodeInfo *node = pw_find_default_node (vol, input_control);
    if (!node) return 0;

    pw_query_node_props (vol, node);
    vol->pw_mute = node->mute;
    return vol->pw_mute;
}

int pw_set_mute (VolumePlugin *vol, int mute, gboolean input_control)
{
    PwNodeInfo *node = pw_find_default_node (vol, input_control);

    DEBUG ("pw_set_mute %d %d", mute, input_control);

    if (!node) return 0;
    vol->pw_mute = mute;

    return pw_set_node_props (vol, node, NULL, 0, mute, FALSE, TRUE);
}

/*----------------------------------------------------------------------------*/
/* Sink and source control                                                    */
/*----------------------------------------------------------------------------*/

int pw_get_default_sink_source (VolumePlugin *vol)
{
    DEBUG ("pw_get_default_sink_source");

    /* the values are kept up to date by on_metadata_property(); force a
     * roundtrip so any pending updates have already been applied */
    pw_roundtrip (vol);
    return 1;
}

static int pw_set_default (VolumePlugin *vol, gboolean input_control, const char *name)
{
    char *json;
    int ok = 1;

    if (!vol->pw_default_metadata) return 0;

    json = g_strdup_printf ("{ \"name\": \"%s\" }", name);

    pw_thread_loop_lock (vol->pw_loop);
    if (pw_metadata_set_property (vol->pw_default_metadata, PW_ID_CORE,
        input_control ? "default.configured.audio.source" : "default.configured.audio.sink",
        "Spa:String:JSON", json) < 0)
        ok = 0;
    pw_thread_loop_unlock (vol->pw_loop);

    g_free (json);

    pw_roundtrip (vol);
    return ok && !vol->pw_error_msg;
}

int pw_change_sink (VolumePlugin *vol, const char *sinkname)
{
    DEBUG ("pw_change_sink %s", sinkname);

    g_free (vol->pw_default_sink);
    vol->pw_default_sink = g_strdup (sinkname);

    if (!pw_set_default (vol, FALSE, sinkname))
    {
        DEBUG ("pw_change_sink error");
        return 0;
    }

    DEBUG ("pw_change_sink done");
    return 1;
}

int pw_change_source (VolumePlugin *vol, const char *sourcename)
{
    DEBUG ("pw_change_source %s", sourcename);

    g_free (vol->pw_default_source);
    vol->pw_default_source = g_strdup (sourcename);

    if (!pw_set_default (vol, TRUE, sourcename))
    {
        DEBUG ("pw_change_source error");
        return 0;
    }

    DEBUG ("pw_change_source done");
    return 1;
}

/* Move existing running streams onto the (new) default sink / source by
 * pointing their "target.object" metadata at it. */

static void pw_move_streams (VolumePlugin *vol, gboolean input_control)
{
    GHashTableIter iter;
    gpointer key, value;
    const char *stream_class = input_control ? "Stream/Input/Audio" : "Stream/Output/Audio";
    const char *target = input_control ? vol->pw_default_source : vol->pw_default_sink;

    if (!vol->pw_default_metadata || !target) return;

    g_hash_table_iter_init (&iter, vol->pw_node_info);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        PwNodeInfo *node = (PwNodeInfo *) value;
        if (g_strcmp0 (node->media_class, stream_class)) continue;

        pw_thread_loop_lock (vol->pw_loop);
        pw_metadata_set_property (vol->pw_default_metadata, node->id, PW_KEY_TARGET_OBJECT, "Spa:Id", target);
        pw_thread_loop_unlock (vol->pw_loop);
    }

    pw_roundtrip (vol);
}

void pw_move_output_streams (VolumePlugin *vol)
{
    DEBUG ("pw_move_output_streams");
    pw_move_streams (vol, FALSE);
    DEBUG ("pw_move_output_streams done");
}

void pw_move_input_streams (VolumePlugin *vol)
{
    DEBUG ("pw_move_input_streams");
    pw_move_streams (vol, TRUE);
    DEBUG ("pw_move_input_streams done");
}

/*----------------------------------------------------------------------------*/
/* Output control                                                             */
/*----------------------------------------------------------------------------*/

static void pw_set_streams_mute (VolumePlugin *vol, int mute)
{
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init (&iter, vol->pw_node_info);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        PwNodeInfo *node = (PwNodeInfo *) value;
        if (g_strcmp0 (node->media_class, "Stream/Output/Audio")) continue;
        pw_set_node_props (vol, node, NULL, 0, mute, FALSE, TRUE);
    }
}

void pw_mute_all_streams (VolumePlugin *vol)
{
    DEBUG ("pw_mute_all_streams");
    pw_set_streams_mute (vol, 1);
    DEBUG ("pw_mute_all_streams done");
}

void pw_unmute_all_streams (VolumePlugin *vol)
{
    DEBUG ("pw_unmute_all_streams");
    pw_set_streams_mute (vol, 0);
    DEBUG ("pw_unmute_all_streams done");
}

/*----------------------------------------------------------------------------*/
/* Profiles                                                                   */
/*----------------------------------------------------------------------------*/

int pw_get_profile (VolumePlugin *vol, const char *card)
{
    PwDeviceInfo *dev;

    g_free (vol->pw_profile);
    vol->pw_profile = NULL;

    dev = pw_find_device_by_name (vol, card);
    if (!dev || !dev->proxy) return 0;

    pw_thread_loop_lock (vol->pw_loop);
    pw_device_enum_params ((struct pw_device *) dev->proxy, 0, SPA_PARAM_Profile, 0, 1, NULL);
    pw_thread_loop_unlock (vol->pw_loop);

    pw_roundtrip (vol);

    if (dev->active_profile_name) vol->pw_profile = g_strdup (dev->active_profile_name);

    return vol->pw_error_msg ? 0 : 1;
}

int pw_set_profile (VolumePlugin *vol, const char *card, const char *profile)
{
    PwDeviceInfo *dev;
    GList *l;
    int32_t index = -1;

    DEBUG ("pw_set_profile %s %s", card, profile);

    dev = pw_find_device_by_name (vol, card);
    if (!dev || !dev->proxy) return 0;

    for (l = dev->profiles; l; l = l->next)
    {
        PwProfileInfo *prof = (PwProfileInfo *) l->data;
        if (!g_strcmp0 (prof->name, profile))
        {
            index = g_list_position (dev->profiles, l);
            break;
        }
    }
    if (index < 0) return 0;

    uint8_t buffer[512];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT (buffer, sizeof (buffer));
    struct spa_pod *param = spa_pod_builder_add_object (&b,
        SPA_TYPE_OBJECT_ParamProfile, SPA_PARAM_Profile,
        SPA_PARAM_PROFILE_index, SPA_POD_Int (index));

    if (vol->pw_error_msg)
    {
        g_free (vol->pw_error_msg);
        vol->pw_error_msg = NULL;
    }

    pw_thread_loop_lock (vol->pw_loop);
    pw_device_set_param ((struct pw_device *) dev->proxy, SPA_PARAM_Profile, 0, param);
    pw_thread_loop_unlock (vol->pw_loop);

    pw_roundtrip (vol);

    return vol->pw_error_msg ? 0 : 1;
}

/*----------------------------------------------------------------------------*/
/* Device menu                                                                */
/*----------------------------------------------------------------------------*/

int pw_add_devices_to_menu (VolumePlugin *vol, gboolean internal, gboolean input_control)
{
    GHashTableIter iter;
    gpointer key, value;

    if (internal && input_control) return 0;
    vol->separator = FALSE;
    DEBUG ("pw_add_devices_to_menu %d %d", input_control, internal);

    pw_roundtrip (vol);

    g_hash_table_iter_init (&iter, vol->pw_device_info);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        PwDeviceInfo *dev = (PwDeviceInfo *) value;

        if (!dev->name || !g_str_has_prefix (dev->name, "alsa_card.")) continue;
        if (!pw_device_has_direction (vol, dev, input_control)) continue;

        gboolean is_internal = !g_strcmp0 (dev->form_factor, "internal") || !dev->form_factor;

        if (input_control)
        {
            menu_add_item (vol, dev->description ? dev->description : dev->name, dev->name, TRUE);
        }
        else if (internal && is_internal)
        {
            menu_add_item (vol, dev->description ? dev->description : dev->name, dev->name, FALSE);
        }
        else if (!internal && !is_internal)
        {
            menu_add_separator (vol, vol->menu_devices[0]);
            menu_add_item (vol, dev->description ? dev->description : dev->name, dev->name, FALSE);
        }
    }

    return 1;
}

void pw_update_devices_in_menu (VolumePlugin *vol, gboolean input_control)
{
    /* menu items are named after the device directly, matching the node
     * names being looked up elsewhere, so there is nothing further to
     * replace here as there was in the PulseAudio card -> sink/source
     * remapping step. */
    (void) vol;
    (void) input_control;
}

/*----------------------------------------------------------------------------*/
/* Profiles dialog                                                            */
/*----------------------------------------------------------------------------*/

int pw_add_devices_to_profile_dialog (VolumePlugin *vol)
{
    GHashTableIter iter;
    gpointer key, value;

    DEBUG ("pw_add_devices_to_profile_dialog");

    pw_roundtrip (vol);

    g_hash_table_iter_init (&iter, vol->pw_device_info);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        PwDeviceInfo *dev = (PwDeviceInfo *) value;
        GtkListStore *ls;
        GList *l;
        int index = 0, sel = -1;

        if (!dev->name || !dev->profiles) continue;

        ls = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);
        for (l = dev->profiles; l; l = l->next)
        {
            PwProfileInfo *prof = (PwProfileInfo *) l->data;
            if (!g_strcmp0 (prof->name, dev->active_profile_name)) sel = index;
            gtk_list_store_insert_with_values (ls, NULL, index++, 0, prof->name, 1, prof->description, -1);
        }

        if (dev->api && !strncmp (dev->api, "bluez", 5))
            profiles_dialog_add_combo (vol, ls, vol->profiles_bt_box, sel, dev->description ? dev->description : dev->name, dev->name);
        else if (g_strcmp0 (dev->form_factor, "internal"))
            profiles_dialog_add_combo (vol, ls, vol->profiles_ext_box, sel, dev->description ? dev->description : dev->name, dev->name);
        else
            profiles_dialog_add_combo (vol, ls, vol->profiles_int_box, sel, dev->description ? dev->description : dev->name, dev->name);
    }

    return 1;
}

/*----------------------------------------------------------------------------*/
/* Utility functions                                                          */
/*----------------------------------------------------------------------------*/

/* Get a count of the number of input or output devices */

int pw_count_devices (VolumePlugin *vol, gboolean input_control)
{
    GHashTableIter iter;
    gpointer key, value;

    vol->pw_devices = 0;

    pw_roundtrip (vol);

    g_hash_table_iter_init (&iter, vol->pw_device_info);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        PwDeviceInfo *dev = (PwDeviceInfo *) value;

        if (!dev->name || !g_str_has_prefix (dev->name, "alsa_card.")) continue;
        if (pw_device_has_direction (vol, dev, input_control)) vol->pw_devices++;
    }

    return 1;
}

/* End of file */
/*----------------------------------------------------------------------------*/

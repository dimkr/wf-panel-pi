/*============================================================================
Copyright (c) 2018-2025 Raspberry Pi
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

#include <locale.h>
#include <glib/gi18n.h>

#include "plugin.h"

#include "batt_sys.h"
#include "batt.h"

/*----------------------------------------------------------------------------*/
/* Typedefs and macros                                                        */
/*----------------------------------------------------------------------------*/

#define SIM_INTERVAL 500
#define INTERVAL 5000

/* Battery states */
typedef enum
{
    STAT_UNKNOWN = -1,
    STAT_DISCHARGING = 0,
    STAT_CHARGING = 1,
    STAT_EXT_POWER = 2
} status_t;

/*----------------------------------------------------------------------------*/
/* Global data                                                                */
/*----------------------------------------------------------------------------*/

conf_table_t conf_table[2] = {
    {CONF_TYPE_INT,  "batt_num", N_("Battery number to monitor"),   NULL,   "0" },
    {CONF_TYPE_NONE, NULL,       NULL,                              NULL,   NULL}
};

/*----------------------------------------------------------------------------*/
/* Prototypes                                                                 */
/*----------------------------------------------------------------------------*/

static int init_measurement (PtBattPlugin *pt);
static int charge_level (PtBattPlugin *pt, status_t *status, int *tim);
static void update_icon (PtBattPlugin *pt);
static gboolean timer_event (PtBattPlugin *pt);

/*----------------------------------------------------------------------------*/
/* Function definitions                                                       */
/*----------------------------------------------------------------------------*/

/* Initialise measurements and check for a battery */

static int init_measurement (PtBattPlugin *pt)
{
    if (pt->simulate) return 1;

    pt->batt = battery_get (pt->batt_num);
    if (pt->batt) return 1;

    return 0;
}

/* Read current capacity, status and time remaining from battery */

static int charge_level (PtBattPlugin *pt, status_t *status, int *tim)
{
    if (pt->simulate)
    {
        static int level = 0;
       *tim = 30;
        if (level < 100) level +=5;
        else level = -100;
        if (level < 0)
        {
            *status = STAT_DISCHARGING;
            return (level * -1);
        }
        else if (level == 100) *status = STAT_EXT_POWER;
        else *status = STAT_CHARGING;
        return level;
    }
    *status = STAT_UNKNOWN;
    *tim = 0;
    battery *b = pt->batt;
    int mins;
    if (b)
    {
        battery_update (b);
        if (battery_is_charging (b))
        {
            if (strcasecmp (b->state, "full") == 0) *status = STAT_EXT_POWER;
            else *status = STAT_CHARGING;
        }
        else *status = STAT_DISCHARGING;
        mins = b->seconds;
        mins /= 60;
        *tim = mins;
        return b->percentage;
    }
    else return -1;
}


static const char *icon_names[33] = {
    "battery-level-0-symbolic",
    "battery-level-0-charging-symbolic",
    "battery-level-0-plugged-in-symbolic",
    "battery-level-10-symbolic",
    "battery-level-10-charging-symbolic",
    "battery-level-10-plugged-in-symbolic",
    "battery-level-20-symbolic",
    "battery-level-20-charging-symbolic",
    "battery-level-20-plugged-in-symbolic",
    "battery-level-30-symbolic",
    "battery-level-30-charging-symbolic",
    "battery-level-30-plugged-in-symbolic",
    "battery-level-40-symbolic",
    "battery-level-40-charging-symbolic",
    "battery-level-40-plugged-in-symbolic",
    "battery-level-50-symbolic",
    "battery-level-50-charging-symbolic",
    "battery-level-50-plugged-in-symbolic",
    "battery-level-60-symbolic",
    "battery-level-60-charging-symbolic",
    "battery-level-60-plugged-in-symbolic",
    "battery-level-70-symbolic",
    "battery-level-70-charging-symbolic",
    "battery-level-70-plugged-in-symbolic",
    "battery-level-80-symbolic",
    "battery-level-80-charging-symbolic",
    "battery-level-80-plugged-in-symbolic",
    "battery-level-90-symbolic",
    "battery-level-90-charging-symbolic",
    "battery-level-90-plugged-in-symbolic",
    "battery-level-100-symbolic",
    "battery-level-100-charging-symbolic",
    "battery-level-100-plugged-in-symbolic"
};

static void set_icon (PtBattPlugin *pt, int lev, status_t status)
{
    int idx = MIN(
        (
            (
                lev < 0 ? 0 :
                    lev > 100 ? 100 :
                        lev
            ) + 5
        ) / 10,
        10
    ) * 3 + (
        status == STAT_CHARGING ? 1 :
            status == STAT_EXT_POWER ? 2 :
                0
    );

    if (!pt->icons[idx])
    {
        pt->icons[idx] = gtk_icon_theme_load_icon_for_scale (
            gtk_icon_theme_get_default (),
            icon_names[idx],
            wrap_icon_size (pt),
            gtk_widget_get_scale_factor (pt->tray_icon),
            GTK_ICON_LOOKUP_FORCE_SIZE,
            NULL);
    }

    if (pt->icons[idx]) set_image_from_pixbuf (pt->tray_icon, pt->icons[idx]);
}

/* Read the current charge state and update the icon accordingly */

static void update_icon (PtBattPlugin *pt)
{
    int capacity, time;
    status_t status;
    float ftime;
    char str[255];

    if (!pt->timer)
    {
        gtk_widget_hide (pt->plugin);
        return;
    }

    // read the charge status
    capacity = charge_level (pt, &status, &time);
    if (status == STAT_UNKNOWN) return;
    ftime = time / 60.0;

    // fill the battery symbol and create the tooltip
    if (status == STAT_CHARGING)
    {
        if (time <= 0)
            sprintf (str, _("Charging : %d%%"), capacity);
        else if (time < 90)
            sprintf (str, _("Charging : %d%%\nTime remaining : %d minutes"), capacity, time);
        else
            sprintf (str, _("Charging : %d%%\nTime remaining : %0.1f hours"), capacity, ftime);
        set_icon (pt, capacity, status);
    }
    else if (status == STAT_EXT_POWER)
    {
        sprintf (str, _("Charged : %d%%\nOn external power"), capacity);
        set_icon (pt, capacity, status);
    }
    else
    {
        if (time <= 0)
            sprintf (str, _("Discharging : %d%%"), capacity);
        else if (time < 90)
            sprintf (str, _("Discharging : %d%%\nTime remaining : %d minutes"), capacity, time);
        else
            sprintf (str, _("Discharging : %d%%\nTime remaining : %0.1f hours"), capacity, ftime);
        set_icon (pt, capacity, status);
    }

    // set the tooltip
    gtk_widget_set_tooltip_text (pt->tray_icon, str);
    gtk_widget_show_all (pt->plugin);
}

static gboolean timer_event (PtBattPlugin *pt)
{
    update_icon (pt);
    return TRUE;
}

/*----------------------------------------------------------------------------*/
/* wf-panel plugin functions                                                  */
/*----------------------------------------------------------------------------*/

/* Handler for system config changed message from panel */
void batt_update_display (PtBattPlugin *pt)
{
    int i;
    for (i = 0; i < 33; i++)
    {
        if (pt->icons[i])
        {
            g_object_unref (pt->icons[i]);
            pt->icons[i] = NULL;
        }
    }
    update_icon (pt);
}

void batt_set_values (PtBattPlugin *pt)
{
    conf_table[0].value = (void *) &pt->batt_num;
}

/* Handler for battery number update from variable watcher */
void batt_set_num (PtBattPlugin *pt)
{
    if (pt->timer) g_source_remove (pt->timer);
    if (init_measurement (pt))
    {
        pt->timer = g_timeout_add (pt->simulate ? SIM_INTERVAL : INTERVAL, (GSourceFunc) timer_event, (gpointer) pt);
        update_icon (pt);
    }
    else pt->timer = 0;
}

void batt_init (PtBattPlugin *pt)
{
    setlocale (LC_ALL, "");
    bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

    /* Allocate icon as a child of top level */
    pt->tray_icon = gtk_image_new ();
    gtk_container_add (GTK_CONTAINER (pt->plugin), pt->tray_icon);

    /* Set up button */
    wrap_add_longpress (pt->gesture, pt->plugin, NULL, NULL);

    if (getenv ("PLUGIN_SIMBAT")) pt->simulate = TRUE;
    else pt->simulate = FALSE;

    /* Start timed events to monitor status */
    batt_set_num (pt);
}

void batt_destructor (gpointer user_data)
{
    int i;
    PtBattPlugin *pt = (PtBattPlugin *) user_data;

    wrap_free_gesture (pt->gesture);

    /* Disconnect the timer */
    if (pt->timer) g_source_remove (pt->timer);

    for (i = 0; i < 33; i++)
    {
        if (pt->icons[i]) g_object_unref (pt->icons[i]);
    }

    g_free (pt);
}

/* End of file */
/*----------------------------------------------------------------------------*/

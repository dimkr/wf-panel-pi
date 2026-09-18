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

#ifndef AUTOHIDE_WINDOW_H
#define AUTOHIDE_WINDOW_H

#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>
#include <gtk-layer-shell/gtk-layer-shell.h>

typedef struct _AutohidingWindow AutohidingWindow;

struct _AutohidingWindow
{
    GtkWidget *window;

    gboolean dock;

    char *position;
    char *layer;
    char *monitor;
    int offset;
    int remainder;
    gboolean autohide;
    int duration;

    int autohide_counter;
    gboolean has_auto_exclusive_zone;
    gboolean input_inside_panel;
    gboolean noleave;

    gboolean last_autohide_value;
    int last_zone;

    int start_marg;
    int targ_marg;
    GTimeZone *tz;
    GDateTime *anim_start;

    /* Retains a reference on the monitor last passed to gtk_layer_set_monitor.
     * During a hotplug, GDK frees the monitor structs for any monitors
     * which are no longer used, and the pointers can be reused for new monitors.
     * gtk-layer-shell checks to see if the monitor pointer for a layer has
     * changed, and if not, it does not update the layer - if the monitor has
     * changed but the pointer has not, this results in the layer not being moved
     * to the new pointer. By making mon a global, the pointer is retained even
     * when freed by GDK, preventing an old pointer being reused for a new monitor.
     */
    GdkMonitor *mon;

    guint pending_show, pending_hide;
};

extern AutohidingWindow *autohide_window_new (gboolean is_dock);
extern void autohide_window_free (AutohidingWindow *win);
extern void autohide_window_set_auto_exclusive_zone (AutohidingWindow *win, gboolean has_zone);
extern void autohide_window_set_monitor (AutohidingWindow *win);
extern void autohide_window_update_position (AutohidingWindow *win);
extern void autohide_window_handle_config_reload (AutohidingWindow *win);

#endif /* end of include guard: AUTOHIDE_WINDOW_H */

/* End of file */
/*----------------------------------------------------------------------------*/

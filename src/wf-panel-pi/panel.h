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

#ifndef PANEL_H
#define PANEL_H

#include <gtk/gtk.h>

#include "widget.h"
#include "autohide-window.h"

typedef struct _Panel Panel;

struct _Panel
{
    AutohidingWindow *window;

    GtkWidget *content_box;
    GtkWidget *left_box, *right_box, *right2_box;
    GtkWidget *grid;
    GtkWidget *menu;
    GtkWidget *conf;
    GtkWidget *cplug;
    GtkWidget *notif;
    GtkWidget *appset;
    GtkWidget *sep;
    GtkGesture *gesture;
    gulong draw_connection;

    GList *left_widgets, *right_widgets;

    gboolean dock;
    gboolean wizard;
    int scaling;

    gboolean pending_update;

    int icon_size;
    char *left_widgets_opt;
    char *right_widgets_opt;
    gboolean exclusive;

    int notify_timeout;
    gboolean notifications;
    gboolean libnotify;
};

extern Panel *panel_new (gboolean is_dock);
extern void panel_free (Panel *panel);
extern void panel_handle_config_reload (Panel *panel);
extern void panel_handle_command_message (Panel *panel, const char *plugin, const char *cmd);
extern void panel_monitor_update_pending (Panel *panel, gboolean pend);

#endif /* end of include guard: PANEL_H */

/* End of file */
/*----------------------------------------------------------------------------*/

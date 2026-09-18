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

#ifndef PANEL_APP_H
#define PANEL_APP_H

#include <gtk/gtk.h>
#include <menu-cache.h>

typedef struct _Panel Panel;

typedef struct _PanelApp PanelApp;

struct _PanelApp
{
    GtkApplication *app;

    Panel *panel;
    Panel *dock;

    guint hotplug_timer;

    GDBusNodeInfo *introspection_data;
    GDBusInterfaceVTable *interface_vtable;
    guint owner_id;

    int inotify_fd;
    guint inotify_source;
};

extern void panel_app_create (int argc, char **argv);

#endif /* end of include guard: PANEL_APP_H */

/* End of file */
/*----------------------------------------------------------------------------*/

/*============================================================================
Copyright (c) 2025 Raspberry Pi
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

/*----------------------------------------------------------------------------*/
/* Typedefs and macros                                                        */
/*----------------------------------------------------------------------------*/

#define PLUGIN_TITLE N_("Clock")

typedef struct 
{
    GtkWidget *plugin;
    GtkGesture *gesture;
    GtkWidget *clock_label;         /* Clock display */
    GtkWidget *clock_ana;
    GtkWidget *calendar_window;     /* Calendar window */
    char *time_format;			    /* Format string for time value */
    char *date_format;			    /* Format string for date value */
    char *clock_font;			    /* Clock display font */
    gboolean override_font;         /* Use override font */
    guint timer;                    /* Seconds timer ID */
    gboolean analogue;
    GdkRGBA face_col;               /* Clock face colour */
    GdkRGBA hands_col;              /* Clock hands colour */
    LXPLUG_VARS
} ClockPlugin;

extern conf_table_t conf_table[8];

/*----------------------------------------------------------------------------*/
/* Prototypes                                                                 */
/*----------------------------------------------------------------------------*/

extern void clock_init (ClockPlugin *clk);
extern void clock_set_values (ClockPlugin *clk);
extern void clock_update_display (ClockPlugin *clk);
extern void clock_destructor (gpointer user_data);

/* End of file */
/*----------------------------------------------------------------------------*/

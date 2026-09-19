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

/*----------------------------------------------------------------------------*/
/* Prototypes                                                                 */
/*----------------------------------------------------------------------------*/

extern char *get_string (const char *fmt, ...);
extern int vsystem (const char *fmt, ...);
extern void close_widget (GtkWidget **wid);
extern const char *device_display_name (VolumePlugin *vol, const char *name);

extern void update_display (VolumePlugin *vol, gboolean input);

extern void menu_show (VolumePlugin *vol, gboolean input);
extern void menu_add_item (VolumePlugin *vol, const char *label, const char *name, gboolean input);
extern void menu_add_separator (VolumePlugin *vol, GtkWidget *menu);
extern void menu_set_alsa_device_output (GtkWidget *widget, VolumePlugin *vol);
extern void menu_set_bluetooth_device_output (GtkWidget *widget, VolumePlugin *vol);
extern void menu_set_alsa_device_input (GtkWidget *widget, VolumePlugin *vol);
extern void menu_set_bluetooth_device_input (GtkWidget *widget, VolumePlugin *vol);

extern void popup_window_show (VolumePlugin *vol, gboolean input_control);
extern void popup_window_show_timed (VolumePlugin *vol);

extern void volume_mouse_scrolled (GtkScale *scale, GdkEventScroll *evt, VolumePlugin *vol);
extern void mic_mouse_scrolled (GtkScale *scale, GdkEventScroll *evt, VolumePlugin *vol);

extern void profiles_dialog_show (VolumePlugin *vol);
extern void profiles_dialog_add_combo (VolumePlugin *vol, GtkListStore *ls, GtkWidget *dest, int sel, const char *label, const char *name);

/* End of file */
/*----------------------------------------------------------------------------*/

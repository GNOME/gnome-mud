/* GNOME-Mud - A simple Mud CLient
 * Copyright (C) 1998-2002 Robin Ericsson <lobbin@localhost.nu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __GNOME_MUD_H__
#define __GNOME_MUD_H__

#include "mccpDecompress.h"
#include <stdio.h>
/*
 * Different type of message, so I'll know what color to use.
 */
#define MESSAGE_ERR     0
#define MESSAGE_NORMAL  1
#define MESSAGE_ANSI    2
#define MESSAGE_NONE    3
#define MESSAGE_SENT    4
#define MESSAGE_SYSTEM	5

/*
 * Maximum number of connections possible
 */
#define MAX_CONNECTIONS 16

/*
 * Maximum number of colors
 */
#define C_MAX 16

/*
 * Save file version
 */
#define WIZARD_SAVEFILE_VERSION 1

/*
 * Typedefs
 */
typedef struct connection_data CONNECTION_DATA;
typedef struct wizard_data     WIZARD_DATA;
typedef	struct wizard_data2	   WIZARD_DATA2;
typedef struct system_data     SYSTEM_DATA;
typedef struct keybind_data    KEYBIND_DATA;
typedef	struct profile_data	   PROFILE_DATA;
typedef        gint            bool;

/*
 * Structures
 */
struct connection_data 
{
	PROFILE_DATA	*profile;
	FILE			*log;
	mc_state		*mccp;
	gint			 mccp_timer;
	gchar			*host;
	gchar			*port;
	gchar			*log_filename;
	gint			 data_ready;
	gint			 sockfd;
	gint			 connected;
	gint			 notebook;
	gint			 logging;
	gboolean		 echo;
	GtkWidget		*window;
	GtkWidget		*vscrollbar;
	GdkColor		*foreground;
	GdkColor		*background;
};

struct system_data {
	bool       EchoText;
	bool       KeepText;
	bool       AutoSave;
	bool       Freeze;
	bool       DisableKeys;
	gchar     *FontName;
	gchar     *CommDev;
	gchar     *TerminalType;
	gchar     *MudListFile;
	gchar     *LastLogDir;
	gint       History;
	GdkColor   Foreground;
	GdkColor   BoldForeground;
	GdkColor   Background;

	GdkColor   Colors[16];
};

struct wizard_data {
    WIZARD_DATA *next;
    gchar      *name;
    gchar      *hostname;
    gchar      *port;
    gchar      *cstring;
    gchar      *playername;
    gchar      *password;
    bool       autologin;
};

struct wizard_data2 {
	gchar	*name;
	gchar	*hostname;
	gchar	*port;
	gchar	*playername;
	gchar	*password;
	gchar	*profile;
};

struct keybind_data {
    gint	 state;
    gint	 keyv;
    gchar	 *data;
    KEYBIND_DATA *next;
};

struct profile_data {
	gchar 			*name;
	GtkCList		*alias;
	GtkCList		*variables;
	GtkCList		*triggers;
	GtkCList		*keys;
	KEYBIND_DATA	*kd;
};

/*
 * Function declares
 */

/* data.c */
void		 load_data (GtkCList *, gchar *);
void		 window_data(PROFILE_DATA *, gint);
gchar		*check_actions (GtkCList *, gchar *);
gchar		*check_alias (GtkCList *, gchar *);
gchar		*check_vars (GtkCList *, gchar *);

/* net.c */
CONNECTION_DATA	*make_connection (gchar *, gchar *, gchar *);
void		 disconnect (GtkWidget *, CONNECTION_DATA *);
void		 send_to_connection (GtkWidget *, gpointer);
void		 connection_send (CONNECTION_DATA *, gchar *);
void		 connection_send_data (CONNECTION_DATA *, gchar *, int);
void		 connection_send_telnet_control (CONNECTION_DATA *, int len, ...);

/* init.c */
void		 main_window (void);
void		 destroy (GtkWidget *);
void		 close_window (GtkWidget *, gpointer);

/* color.c */
void  create_color_box( void                               );
void  window_color    ( GtkWidget *widget, gpointer data   );
void  save_colors     ( void				   );	
void  load_colors     ( void				   );
void  load_color_to_c_structs ( void			   );

/* keybind.c */
void window_keybind   ( PROFILE_DATA *		   );

/* log.c */
void  window_menu_file_start_logging_cb ( GtkWidget *widget, gpointer data );
void  window_menu_file_stop_logging_cb  ( GtkWidget *widget, gpointer data );
void  window_menu_file_save_buffer_cb   ( GtkWidget *widget, gpointer data );
void  stop_logging_connection           ( CONNECTION_DATA *connection      );

/* map.c */
void  window_automap  ( GtkWidget *widget, gpointer data   );

/* misc.c */
void  init_uid        ( void                               );

/* modules.c */
void  do_plugin_information (GtkWidget *w, gpointer data   );
int   init_modules    ( char *path                         );
void  save_plugins    ( void                               );

/* mudlist.c */
void  window_mudlist  ( GtkWidget *widget, gboolean wizard );

/* prefs.c */
void  load_prefs      ( void                               );
void  save_prefs      ( void                               );
void  window_prefs    ( GtkWidget *widget, gpointer data   );
FILE *open_file       ( gchar *filename, gchar *mode       );

/* profiles */
void		  load_profiles   (	void							   );
PROFILE_DATA *profiledata_find( gchar *							   );
void  		  profiledata_save( gchar *, GtkCList *, gchar *	   );
void		  profiledata_savekeys(gchar *, KEYBIND_DATA *		   );
void  		  window_profiles ( void							   );
void		  window_profile_edit ( void						   );

/* python.c */
void   python_init     ( void                                        );
void   python_end      ( void                                        );
gchar *python_process_input( CONNECTION_DATA *, gchar *              );
gchar *python_process_output( CONNECTION_DATA *, gchar *             );

/* telnet.c */
gint  pre_process     ( char *buf, CONNECTION_DATA *connection       );

/* window.c */
void  popup_window    ( const gchar *message                         );
void  switch_page_cb  ( GtkNotebook *, gpointer, guint, gpointer     );
void  grab_focus_cb   ( GtkWidget* widget, gpointer user_data        );
void  textfield_add   ( CONNECTION_DATA *, gchar *, gint             );

/* wizard.c */
void  free_connection_data (CONNECTION_DATA *c             );
void  add_wizard_connect ( gchar **entry, bool al, WIZARD_DATA *w   );

#endif /* __GNOME_H__ */

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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include <sys/stat.h>
#include <vte/vte.h>

#include <errno.h>
#include <fcntl.h>
#include <gconf/gconf-client.h>
#include <gnome.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gnome-mud.h"

static char const rcsid[] =
    "$Id$";

extern GList 			*EntryHistory;
extern GList			*EntryCurr;
extern GConfClient		*gconf_client;
extern CONNECTION_DATA	*connections[MAX_CONNECTIONS];
extern GtkWidget        *main_notebook;

SYSTEM_DATA prefs;
#ifndef WITHOUT_MAPPER
AutoMapConfig* automap_config;
#endif

static char *color_to_string(const GdkColor *c)
{
	char *s;
	char *ptr;

	s = g_strdup_printf("#%2X%2X%2X", c->red / 256, c->green / 256, c->blue / 256);

	for (ptr = s; *ptr; ptr++)
	{
		if (*ptr == ' ')
		{
			*ptr = '0';
		}
	}

	return s;
}


#ifndef WITHOUT_MAPPER
void update_gconf_from_unusual_exits()
{
	gchar* entry;
	int size = 0;
	GList* puck;
	gchar* iter;
	
	for (puck = automap_config->unusual_exits; puck != NULL; puck = puck->next)
		size = size + strlen(puck->data) + 1;
	
	entry = g_malloc0(size * sizeof(char));
	iter = entry;

	for (puck = automap_config->unusual_exits; puck != NULL; puck = puck->next)
	{
		iter = g_stpcpy(iter, puck->data);
		iter = g_stpcpy(iter, ";");
	}
	
	gconf_client_set_string(gconf_client, "/apps/gnome-mud/mapper/unusual_exits", entry, NULL);
	g_free(entry);
}

#endif
static char *tab_location_by_name(const gint i)
{
	switch (i)
	{
		case TAB_LOCATION_TOP:
			return "top";
			
		case TAB_LOCATION_RIGHT:
			return "right";

		case TAB_LOCATION_BOTTOM:
			return "bottom";

		case TAB_LOCATION_LEFT:
			return "left";
	}

	return "bottom";
}

static char tab_location_by_int(const gchar *p)
{
	switch(tab_location_by_gtk(p))
	{
		case GTK_POS_TOP:
			return 0;

		case GTK_POS_RIGHT:
			return 1;

		case GTK_POS_BOTTOM:
			return 2;

		case GTK_POS_LEFT:
			return 3;
	}

	return 2;
}

GtkPositionType tab_location_by_gtk(const gchar *p)
{
	if (!g_ascii_strncasecmp(p, "top", sizeof("top")))
	{
		return GTK_POS_TOP;
	}
	else if (!g_ascii_strncasecmp(p, "right", sizeof("right")))
	{
		return GTK_POS_RIGHT;
	}
	else if (!g_ascii_strncasecmp(p, "bottom", sizeof("bottom")))
	{
		return GTK_POS_BOTTOM;
	}
	else if (!g_ascii_strncasecmp(p, "left", sizeof("left")))
	{
		return GTK_POS_LEFT;
	}

	return GTK_POS_BOTTOM;
}

static void prefs_changed_tab_location()
{
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(main_notebook), tab_location_by_gtk(prefs.TabLocation));
}

static void prefs_gconf_changed(GConfClient *client, guint cnxn_id, GConfEntry *entry, gpointer data)
{
	gint      i, n_colors;
	gchar    *key;
	GdkColor  color, *colors;

	key = g_path_get_basename(gconf_entry_get_key(entry));

#define vte_terminal_NULL(a, b)
#define prefs_changed_NULL()
	
#define UPDATE_STRING(Entry, Variable, Loop, LoopFunction, Callback)                             \
	}                                                                                            \
	else if (strcmp(key, Entry) == 0)                                                            \
	{                                                                                            \
		g_free(prefs.Variable);                                                                  \
		prefs.Variable = g_strdup(gconf_value_get_string(gconf_entry_get_value(entry)));         \
                                                                                                 \
		if (Loop)                                                                                \
		{                                                                                        \
			for (i = 0; i < MAX_CONNECTIONS; i++)                                                \
			{                                                                                    \
				if (connections[i] != NULL)                                                      \
				{                                                                                \
					vte_terminal_##LoopFunction (VTE_TERMINAL(connections[i]->window), prefs.Variable); \
				}                                                                                \
			}                                                                                    \
		}                                                                                        \
		                                                                                         \
		prefs_changed_##Callback ();

	
#define UPDATE_BOOLEAN(Entry, Variable, Loop, LoopFunction, Callback)                            \
	}                                                                                            \
	else if (strcmp(key, Entry) == 0)                                                            \
	{                                                                                            \
		prefs.Variable = gconf_value_get_bool(gconf_entry_get_value(entry));                     \
                                                                                                 \
		if (Loop)                                                                                \
		{                                                                                        \
			for (i = 0; i < MAX_CONNECTIONS; i++)                                                \
			{                                                                                    \
				if (connections[i] != NULL)                                                      \
				{                                                                                \
					vte_terminal_##LoopFunction (VTE_TERMINAL(connections[i]->window), prefs.Variable); \
				}                                                                                \
			}                                                                                    \
		}

#define UPDATE_INT(Entry, Variable, Loop, LoopFunction, Callback)                                \
	}                                                                                            \
	else if (strcmp(key, Entry) == 0)                                                            \
	{                                                                                            \
		prefs.Variable = gconf_value_get_int(gconf_entry_get_value(entry));                      \
                                                                                                 \
		if (Loop)                                                                                \
		{                                                                                        \
			for (i = 0; i < MAX_CONNECTIONS; i++)                                                \
			{                                                                                    \
				if (connections[i] != NULL)                                                      \
				{                                                                                \
					vte_terminal_##LoopFunction (VTE_TERMINAL(connections[i]->window), prefs.Variable); \
				}                                                                                \
			}                                                                                    \
		}

#define UPDATE_COLOR(Entry, Variable)                                                            \
	}                                                                                            \
	else if (strcmp(key, Entry) == 0)                                                            \
	{                                                                                            \
		const gchar *p = gconf_value_get_string(gconf_entry_get_value(entry));                   \
			                                                                                     \
		if (p && gdk_color_parse(p, &color))                                                     \
		{                                                                                        \
			prefs.Variable = color;                                                              \
		                                                                                         \
			for (i = 0; i < MAX_CONNECTIONS; i++)                                                \
			{                                                                                    \
				if (connections[i] != NULL)                                                      \
				{                                                                                \
					vte_terminal_set_colors(VTE_TERMINAL(connections[i]->window), &prefs.Foreground, &prefs.Background, prefs.Colors, C_MAX); \
				}                                                                                \
			}                                                                                    \
		}

#define UPDATE_PALETTE(Entry, Variable)                                                          \
	}                                                                                            \
	else if (strcmp(key, Entry) == 0)                                                            \
	{                                                                                            \
		gtk_color_selection_palette_from_string(gconf_value_get_string(gconf_entry_get_value(entry)), &colors, &n_colors); \
		if (n_colors < C_MAX)                                                                    \
		{                                                                                        \
			g_printerr(_("Palette had %d entries instead of %d\n"), n_colors, C_MAX);            \
		}                                                                                        \
		memcpy(prefs.Variable, colors, C_MAX * sizeof(GdkColor));                                \
	                                                                                             \
		for (i = 0; i < MAX_CONNECTIONS; i++)                                                    \
		{                                                                                        \
			if (connections[i] != NULL)                                                          \
			{                                                                                    \
				vte_terminal_set_colors(VTE_TERMINAL(connections[i]->window), &prefs.Foreground, &prefs.Background, prefs.Colors, C_MAX); \
			}                                                                                    \
		}                                                                                        \
		                                                                                         \
		g_free(colors);

	if (0)
	{
		;
		UPDATE_STRING("font", 				FontName, 		TRUE, 	set_font_from_string,	NULL        );
		UPDATE_STRING("commdev",			CommDev,		FALSE,	NULL,					NULL        );
		UPDATE_BOOLEAN("echo",				EchoText,		FALSE,	NULL,					NULL        );
		UPDATE_BOOLEAN("keeptext",			KeepText,		FALSE,	NULL,					NULL        );
		UPDATE_BOOLEAN("system_keys",		DisableKeys,	FALSE,	NULL,					NULL        );
		UPDATE_STRING("terminal_type",		TerminalType,	TRUE,	set_emulation,			NULL        );
		UPDATE_STRING("mudlist_file",		MudListFile,	FALSE,	NULL,					NULL        );
		UPDATE_COLOR("foreground_color",	Foreground											        );
		UPDATE_COLOR("background_color",	Background											        );
		UPDATE_BOOLEAN("scroll_on_output",	ScrollOnOutput,	TRUE,	set_scroll_on_output,	NULL        );
		UPDATE_INT("scrollback_lines",		Scrollback,		TRUE,	set_scrollback_lines,	NULL        );
		UPDATE_STRING("tab_location",		TabLocation,	FALSE,	NULL,					tab_location);
		UPDATE_PALETTE("palette",			Colors                                                      );
		UPDATE_STRING("last_log_dir",		LastLogDir,		FALSE,	NULL,					NULL        );
		UPDATE_INT("history_count",			History,		FALSE,	NULL,					NULL		);
	}

#ifndef WITHOUT_MAPPER
	/* And the automapper */
	if (!strcmp(key, "unusual_exits"))
	{
		gchar* p;
		if (automap_config->unusual_exits)
		{
			/* Empty the previous list */
			GList* puck;
			for (puck = automap_config->unusual_exits; puck != NULL; puck = puck->next)
				g_free(puck->data);
			g_list_free(automap_config->unusual_exits);
		
			automap_config->unusual_exits = NULL;
		}
		
		p = gconf_client_get_string(gconf_client, "/apps/gnome-mud/mapper/unusual_exits", NULL);
		if (p)
		{
			gchar** unusual_exits;
			unusual_exits = g_strsplit(p, ";", 100);
	
			for (i = 0; unusual_exits[i] != NULL; i++)
			{
				unusual_exits[i] = g_strstrip(unusual_exits[i]);
				if (unusual_exits[i][0] != '\0')
					automap_config->unusual_exits = g_list_append(automap_config->unusual_exits, g_strdup(unusual_exits[i]));
			}
			
			g_strfreev(unusual_exits);
		}
		else
		{
			automap_config->unusual_exits = NULL;
		}
	}
#endif

#undef UPDATE_PALETTE
#undef UPDATE_STRING
#undef UPDATE_BOOLEAN
#undef UPDATE_COLOR
#undef UPDATE_INT
#undef vte_terminal_NULL
#undef prefs_changed_NULL

	g_free(key);
}

void load_prefs ( void )
{
	GdkColor  color;
	GdkColor *colors;
	gint      n_colors;
	struct    stat file_stat;
	gchar     dirname[256], buf[256];
	gchar    *p;
	
	/*
	 * Check for ~/.gnome-mud
	 */
	g_snprintf (dirname, 255, "%s/.gnome-mud", g_get_home_dir());
	if ( stat (dirname, &file_stat) == 0) /* can we stat ~/.gnome-mud? */
	{
		if ( !(S_ISDIR(file_stat.st_mode))) /* if it's not a directory */
		{
			g_snprintf (buf, 255, _("%s already exists and is not a directory!"), dirname);
			popup_window (buf);
			return;
		}
	} 
	else /* it must not exist */
	{
		if ((mkdir (dirname, 0777)) != 0) /* this isn't dangerous, umask modifies it */
		{
			g_snprintf (buf, 255, _("%s does not exist and can NOT be created: %s"), dirname, strerror(errno));
			popup_window (buf);
			return;
		}
	}

#define	GCONF_GET_STRING(entry, subdir, variable)                                          \
	p = gconf_client_get_string(gconf_client, "/apps/gnome-mud/" #subdir "/" #entry, NULL);\
	prefs.variable = g_strdup(p);

#define GCONF_GET_BOOLEAN(entry, subdir, variable)                                         \
	prefs.variable = gconf_client_get_bool(gconf_client, "/apps/gnome-mud/" #subdir "/" #entry, NULL);

#define GCONF_GET_INT(entry, subdir, variable)                                             \
	prefs.variable = gconf_client_get_int(gconf_client, "/apps/gnome-mud/" #subdir "/" #entry, NULL);
	
#define GCONF_GET_COLOR(entry, subdir, variable)                                           \
	p = gconf_client_get_string(gconf_client, "/apps/gnome-mud/" #subdir "/" #entry, NULL);\
	if (p && gdk_color_parse(p, &color))                                                   \
	{                                                                                      \
		prefs.variable = color;                                                            \
	}

	gconf_client_notify_add(gconf_client, "/apps/gnome-mud", prefs_gconf_changed, NULL, NULL, NULL);

	GCONF_GET_STRING(font, 				ui,				FontName); 
	GCONF_GET_COLOR(foreground_color,	ui,				Foreground);
	GCONF_GET_COLOR(background_color,	ui,				Background);
	GCONF_GET_INT(scrollback_lines,		ui,				Scrollback);
	GCONF_GET_STRING(tab_location,		ui,				TabLocation);
	GCONF_GET_STRING(commdev, 			functionality,	CommDev);
	GCONF_GET_BOOLEAN(echo,     		functionality,	EchoText);
	GCONF_GET_BOOLEAN(keeptext,			functionality,	KeepText);
	GCONF_GET_BOOLEAN(system_keys,		functionality,	DisableKeys);
	GCONF_GET_STRING(terminal_type,		functionality,	TerminalType);
	GCONF_GET_STRING(mudlist_file,		functionality,	MudListFile);
	GCONF_GET_BOOLEAN(scroll_on_output,	functionality,	ScrollOnOutput);
	GCONF_GET_INT(history_count,		functionality,	History);
		
	/* palette */
	p = gconf_client_get_string(gconf_client, "/apps/gnome-mud/ui/palette", NULL);

	if (p)
	{
		gtk_color_selection_palette_from_string(p, &colors, &n_colors);
		if (n_colors < C_MAX)
		{
			g_printerr(_("Palette had %d entries instead of %d\n"), n_colors, C_MAX);
		}
		memcpy(prefs.Colors, colors, C_MAX * sizeof(GdkColor));
		g_free(colors);
	}

	/* last log dir */
	p = gconf_client_get_string(gconf_client, "/apps/gnome-mud/functionality/last_log_dir", NULL);

	if (!g_ascii_strncasecmp(p, "", sizeof("")))
	{
		prefs.LastLogDir = g_strdup(g_get_home_dir());
	}
	else
	{
		prefs.LastLogDir = g_strdup(p);
	}

#ifndef WITHOUT_MAPPER
	/* load automapper prefs : unusual_exits */
	p = gconf_client_get_string(gconf_client, "/apps/gnome-mud/mapper/unusual_exits", NULL);
	automap_config = g_malloc0(sizeof(AutoMapConfig));
	if (p)
	{
		int i;
		gchar** unusual_exits;
		unusual_exits = g_malloc0(sizeof(char) * (strlen(p) + 2));
		unusual_exits = g_strsplit(p, ";", 100);
	
		for (i = 0; unusual_exits[i] != NULL; i++)
		{
			unusual_exits[i] = g_strstrip(unusual_exits[i]);
			if (unusual_exits[i][0] != '\0')
				automap_config->unusual_exits = g_list_append(automap_config->unusual_exits, g_strdup(unusual_exits[i]));
		}
		g_strfreev(unusual_exits);
	}
	else
	{
		automap_config->unusual_exits = NULL;
	}
#endif
	/*
	 * Command history
	 *
	{
		gint  nr, i;
		gchar **cmd_history;
		gnome_config_get_vector("/gnome-mud/Data/CommandHistory", &nr, &cmd_history);

		for (i = 0; i < nr; i++)
		{
			EntryHistory = g_list_append(EntryHistory, (gpointer) cmd_history[i]);
		}

		EntryCurr = NULL;
	}*/

#undef GCONF_GET_BOOLEAN
#undef GCONF_GET_COLOR
#undef GCONF_GET_INT
#undef GCONF_GET_STRING
}

static void prefs_checkbox_keep_cb (GtkWidget *widget, GnomePropertyBox *box)
{
	gboolean value = GTK_TOGGLE_BUTTON(widget)->active ? TRUE : FALSE;

	gconf_client_set_bool(gconf_client, "/apps/gnome-mud/functionality/keeptext", value, NULL);
}

static void prefs_checkbutton_disablekeys_cb(GtkWidget *widget, GnomePropertyBox *box)
{
	gboolean value = GTK_TOGGLE_BUTTON(widget)->active ? TRUE : FALSE;

	gconf_client_set_bool(gconf_client, "/apps/gnome-mud/functionality/system_keys", value, NULL);
}

static void prefs_entry_terminal_cb(GtkWidget *widget, GnomePropertyBox *box)
{
	const gchar *s = gtk_entry_get_text(GTK_ENTRY(widget));

	gconf_client_set_string(gconf_client, "/apps/gnome-mud/functionality/terminal_type", s, NULL);
}

static void prefs_entry_history_cb(GtkWidget *widget, GnomePropertyBox *box)
{
	gint value = (gint) gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));

	gconf_client_set_int(gconf_client, "/apps/gnome-mud/functionality/history_count", value, NULL);
}

static void prefs_entry_mudlistfile_cb(GtkWidget *widget, GnomePropertyBox *box)
{
	const gchar *s = gtk_entry_get_text(GTK_ENTRY(widget));

	gconf_client_set_string(gconf_client, "/apps/gnome-mud/functionality/mudlist_file", s, NULL);
}

static void prefs_entry_divide_cb (GtkWidget *widget, GnomePropertyBox *box)
{
	const gchar *s = gtk_entry_get_text(GTK_ENTRY(widget));

	gconf_client_set_string(gconf_client, "/apps/gnome-mud/functionality/commdev", s, NULL);
}

static void prefs_checkbox_echo_cb(GtkWidget *widget, GnomePropertyBox *box)
{
	gboolean value = GTK_TOGGLE_BUTTON(widget)->active ? TRUE : FALSE;

	gconf_client_set_bool(gconf_client, "/apps/gnome-mud/functionality/echo", value, NULL);
}

static void prefs_scroll_output_changed_cb(GtkWidget *widget, gpointer data)
{
	gboolean value = GTK_TOGGLE_BUTTON(widget)->active ? TRUE : FALSE;

	gconf_client_set_bool(gconf_client, "/apps/gnome-mud/functionality/scroll_on_output", value, NULL);
}

static void prefs_select_font_cb(GnomeFontPicker *fontpicker, gchar *font, gpointer data)
{
	gconf_client_set_string(gconf_client, "/apps/gnome-mud/ui/font", font, NULL);
}

static void prefs_select_fg_color_cb(GnomeColorPicker *colorpicker, guint r, guint g, guint b, guint alpha, gpointer data)
{
	GdkColor color;
	
	color.red = r;
	color.blue = b;
	color.green = g;

	if (!gdk_color_equal(&color, &prefs.Foreground))
	{
		gchar *s;

		s = color_to_string(&color);
		gconf_client_set_string(gconf_client, "/apps/gnome-mud/ui/foreground_color", s, NULL);
	}
}

static void prefs_select_bg_color_cb(GnomeColorPicker *colorpicker, guint r, guint g, guint b, guint alpha, gpointer data)
{
	GdkColor color;
	
	color.red = r;
	color.blue = b;
	color.green = g;

	if (!gdk_color_equal(&color, &prefs.Background))
	{
		gchar *s;

		s = color_to_string(&color);
		gconf_client_set_string(gconf_client, "/apps/gnome-mud/ui/background_color", s, NULL);
	}
}

static void prefs_select_palette_cb(GnomeColorPicker *colorpicker, guint r, guint g, guint b, guint alpha, gpointer data)
{
	gint   i = GPOINTER_TO_INT(data);
	gchar *s;
	
	prefs.Colors[i].red   = r;
	prefs.Colors[i].green = g;
	prefs.Colors[i].blue  = b;

	s = gtk_color_selection_palette_to_string(prefs.Colors, C_MAX);
	gconf_client_set_string(gconf_client, "/apps/gnome-mud/ui/palette", s, NULL);
}

static void prefs_set_color(GtkWidget *color_picker, gint color)
{
    GdkColor *this_color = &prefs.Colors[color];

	gnome_color_picker_set_i16(GNOME_COLOR_PICKER(color_picker), this_color->red, this_color->green, this_color->blue, 0);
}

static void prefs_tab_location_changed_cb(GtkOptionMenu *option, gpointer data)
{
	gint selected;
	
	selected = gtk_option_menu_get_history(option);

	gconf_client_set_string(gconf_client, "/apps/gnome-mud/ui/tab_location", tab_location_by_name(selected), NULL);
}

static void prefs_scrollback_value_changed_cb(GtkWidget *button, gpointer data)
{
	gint value = (gint) gtk_spin_button_get_value(GTK_SPIN_BUTTON(button));

	gconf_client_set_int(gconf_client, "/apps/gnome-mud/ui/scrollback_lines", value, NULL);
}

#ifndef WITHOUT_MAPPER
static void prefs_unusual_exits_cb(GtkWidget *entry, gpointer data)
{
	const gchar* text = gtk_entry_get_text(GTK_ENTRY(entry));

	gconf_client_set_string(gconf_client, "/apps/gnome-mud/mapper/unusual_exits", text, NULL);
}

#endif
GtkWidget *prefs_color_frame (GtkWidget *prefs_window)
{
	GtkWidget *table_colorfont;
	GtkWidget *label_palette;
	GtkWidget *label_background;
	GtkWidget *label_foreground;
	GtkWidget *picker_foreground;
	GtkWidget *picker_background;
	GtkWidget *picker_font;
	GtkWidget *table2;
	GtkWidget *label_font;

	GtkTooltips *tooltip = gtk_tooltips_new();
	
	gint i, j, k;

	table_colorfont = gtk_table_new (5, 2, FALSE);
	gtk_container_set_border_width (GTK_CONTAINER (table_colorfont), 8);
	gtk_table_set_row_spacings (GTK_TABLE (table_colorfont), 4);

	label_font = gtk_label_new (_("Font:"));
	gtk_widget_show (label_font);
	gtk_table_attach (GTK_TABLE (table_colorfont), label_font, 0, 1, 0, 1, (GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (0), 0, 0);
	gtk_misc_set_alignment (GTK_MISC (label_font), 1, 0.5);
	gtk_misc_set_padding (GTK_MISC (label_font), 8, 0);

	picker_font = gnome_font_picker_new ();
	gtk_tooltips_set_tip (tooltip, picker_font,
			_("Main font that is used on all open connections."), NULL);
	gtk_widget_show (picker_font);
	gtk_table_attach (GTK_TABLE (table_colorfont), picker_font, 1, 2, 0, 1, (GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (0), 0, 0);
	gnome_font_picker_set_preview_text (GNOME_FONT_PICKER (picker_font), _("The quick brown fox jumps over the lazy dog"));
	gnome_font_picker_set_mode (GNOME_FONT_PICKER (picker_font), GNOME_FONT_PICKER_MODE_FONT_INFO);
	gnome_font_picker_set_font_name(GNOME_FONT_PICKER(picker_font), prefs.FontName);
	
	label_palette = gtk_label_new (_("Colour palette:"));
	gtk_widget_show (label_palette);
	gtk_table_attach (GTK_TABLE (table_colorfont), label_palette, 0, 1, 4, 5, (GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (0), 0, 0);
	gtk_misc_set_alignment (GTK_MISC (label_palette), 1, 0.5);
	gtk_misc_set_padding (GTK_MISC (label_palette), 8, 0);

	label_background = gtk_label_new (_("Background colour:"));
	gtk_widget_show (label_background);
	gtk_table_attach (GTK_TABLE (table_colorfont), label_background, 0, 1, 3, 4, (GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (0), 0, 0);
	gtk_misc_set_alignment (GTK_MISC (label_background), 1, 0.5);
	gtk_misc_set_padding (GTK_MISC (label_background), 8, 0);

	label_foreground = gtk_label_new (_("Foreground colour:"));
	gtk_widget_show (label_foreground);
	gtk_table_attach (GTK_TABLE (table_colorfont), label_foreground, 0, 1, 1, 2, (GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (0), 0, 0);
	gtk_misc_set_alignment (GTK_MISC (label_foreground), 1, 0.5);
	gtk_misc_set_padding (GTK_MISC (label_foreground), 8, 0);

	picker_foreground = gnome_color_picker_new ();
	gtk_tooltips_set_tip(tooltip, picker_foreground,
			_("Default foreground colour used when the connection "
			  "doesn't request the use of a specific colour."),
			  NULL);
	gtk_widget_show (picker_foreground);
	gtk_table_attach (GTK_TABLE (table_colorfont), picker_foreground, 1, 2, 1, 2, (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), (GtkAttachOptions) (0), 0, 0);
	gnome_color_picker_set_i16(GNOME_COLOR_PICKER(picker_foreground), prefs.Foreground.red, prefs.Foreground.green, prefs.Foreground.blue, 0);
  
	picker_background = gnome_color_picker_new ();
	gtk_tooltips_set_tip(tooltip, picker_background,
			_("Default background colour used when the connection "
			  "doesn't request the use of a specific colour."),
			  NULL);
	gtk_widget_show (picker_background);
	gtk_table_attach (GTK_TABLE (table_colorfont), picker_background, 1, 2, 3, 4, (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), (GtkAttachOptions) (0), 0, 0);
	gnome_color_picker_set_i16(GNOME_COLOR_PICKER(picker_background), prefs.Background.red, prefs.Background.green, prefs.Background.blue, 0);
  
	table2 = gtk_table_new (2, 8, FALSE);
	gtk_widget_show (table2);
	gtk_table_attach (GTK_TABLE (table_colorfont), table2, 1, 2, 4, 5, (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), (GtkAttachOptions) (GTK_FILL), 0, 0);

	for (i = 0, j = 0, k = 0; i < C_MAX; i++)
	{
		GtkWidget *picker = gnome_color_picker_new();
	
		gtk_tooltips_set_tip(tooltip, picker,
				_("Change the colour of a specific colour that "
				  "the MUD requests to use."), NULL);
		
		prefs_set_color(picker, i);
		gtk_table_attach(GTK_TABLE(table2), picker, j, j + 1, k, k + 1, GTK_FILL, 0, 0, 0);
		gtk_widget_show(picker);

		if (j++ == 7)
		{
			j = 0;
			k = 1;
		}

		gtk_signal_connect(GTK_OBJECT(picker),  "color-set", GTK_SIGNAL_FUNC(prefs_select_palette_cb), GINT_TO_POINTER(i));
	}
	
	/*
	 * Signals
	 */
	gtk_signal_connect(GTK_OBJECT(picker_font),       "font-set",   GTK_SIGNAL_FUNC(prefs_select_font_cb), (gpointer) prefs_window);
	gtk_signal_connect(GTK_OBJECT(picker_foreground), "color-set",  GTK_SIGNAL_FUNC(prefs_select_fg_color_cb), NULL);
	gtk_signal_connect(GTK_OBJECT(picker_background), "color-set",  GTK_SIGNAL_FUNC(prefs_select_bg_color_cb), NULL);
	
	return table_colorfont;
}

static void profile_response_cb(GtkDialog *editor, int id, void *data)
{
	if (id == GTK_RESPONSE_HELP)
	{
		gnome_help_display("gnome-mud", "gnome-mud", NULL);
	}
	else
	{
		gtk_widget_destroy(GTK_WIDGET(editor));
	}
}

void window_prefs (GtkWidget *widget, gpointer data)
{
	static GtkWidget *prefs_window;

	gchar *l[4] = { _("on top"), _("on the right"), _("at the bottom"), _("on the left") };

	gint i;

	GtkWidget *notebook;
	GtkWidget *vbox, *frame, *hbox;
	GtkWidget *tmp, *tmp2;

	GtkAdjustment *spinner;
	GtkTooltips *tooltip;

	if (prefs_window != NULL)
	{
		gdk_window_raise(prefs_window->window);
		gdk_window_show(prefs_window->window);
		return;
	}

	prefs_window = gtk_dialog_new();
	gtk_window_set_title(GTK_WINDOW(prefs_window), _("GNOME-Mud Preferences"));
	gtk_window_set_policy(GTK_WINDOW(prefs_window), FALSE, FALSE, FALSE);
	gtk_signal_connect(GTK_OBJECT(prefs_window), "destroy", GTK_SIGNAL_FUNC(gtk_widget_destroyed), &prefs_window);
	vbox = GTK_DIALOG(prefs_window)->vbox;

	tooltip = gtk_tooltips_new();
	notebook = gtk_notebook_new();
	gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);

	/*
	 * Begin page one
	 */
	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_NONE);
	tmp = gtk_label_new(_("Functionality"));
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), frame, tmp);

	vbox = gtk_vbox_new(FALSE, 12);
	gtk_container_add(GTK_CONTAINER(frame), vbox);

	tmp = gtk_check_button_new_with_mnemonic(_("_Echo the text sent"));
	gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(tmp), prefs.EchoText);
	gtk_box_pack_start(GTK_BOX(vbox), tmp, FALSE, FALSE, 0);
	gtk_tooltips_set_tip (tooltip, tmp,
			_("If enabled, all the text typed in will be echoed "
			  "in the terminal, making it easier to control what "
			  "is sent."),
			NULL);
	g_signal_connect(G_OBJECT(tmp), "toggled", G_CALLBACK(prefs_checkbox_echo_cb), (gpointer) prefs_window);


	tmp = gtk_check_button_new_with_mnemonic(_("_Keep the text entered"));
	gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(tmp), prefs.KeepText);
	gtk_box_pack_start(GTK_BOX(vbox), tmp, FALSE, FALSE, 0);
	gtk_tooltips_set_tip (tooltip, tmp,
			_("If enabled, the text that is sent to the "
			  "connection will be left as a selection in the "
			  "entry box. Otherwise, the text entry box will be "
			  "cleared after each text input."),
			NULL);
	g_signal_connect(G_OBJECT(tmp), "toggled", G_CALLBACK(prefs_checkbox_keep_cb), (gpointer) prefs_window);

	tmp = gtk_check_button_new_with_mnemonic(_("Disable _System Keys"));
	gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(tmp), prefs.DisableKeys);
	gtk_box_pack_start(GTK_BOX(vbox), tmp, FALSE, FALSE, 0);
	gtk_tooltips_set_tip(tooltip, tmp,
		  _("If enabled, GNOME-Mud will offer a few built-in "
		    "keybinds. They can be overridden by custom keybinds, or "
		    "they can be disabled completely with this option."),
		  NULL);
	g_signal_connect(G_OBJECT(tmp), "toggled", G_CALLBACK(prefs_checkbutton_disablekeys_cb), (gpointer) prefs_window);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);

	tmp = gtk_label_new(_("Command division character:"));
	gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 10);

	tmp = gtk_entry_new_with_max_length(1);
	gtk_tooltips_set_tip (tooltip, tmp,
			_("The character used to divide commands sent to the "
			  "mud. For example, \";\", will let the string "
			  "\"w;look\" be sent to the mud as 2 separate "
			  "commands."),
			NULL);
	gtk_entry_set_text(GTK_ENTRY(tmp), prefs.CommDev);
	gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, TRUE, 0);
	gtk_widget_set_usize(tmp, 15, -2);
	g_signal_connect(G_OBJECT(tmp), "changed", G_CALLBACK(prefs_entry_divide_cb), (gpointer) prefs_window);

	tmp = gtk_label_new(_("Command history:"));
	gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 10);

	spinner = (GtkAdjustment *) gtk_adjustment_new(prefs.History, 0, 500, 1, 5, 5);
	tmp = gtk_spin_button_new(spinner, 1, 0);
	gtk_tooltips_set_tip (tooltip, tmp,
			_("The number of entries to be saved in the command "
			  "history."), NULL);
	g_signal_connect(G_OBJECT(tmp), "value-changed", G_CALLBACK(prefs_entry_history_cb), NULL);
	gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 10);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 5);

	tmp = gtk_label_new(_("Terminal type:"));
	gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 10);

	tmp = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(tmp), prefs.TerminalType);
	gtk_box_pack_start(GTK_BOX(hbox), tmp, TRUE, TRUE, 5);
	gtk_tooltips_set_tip(tooltip, tmp,
		_("GNOME-Mud will attempt to transmit a terminal type "
		  "(like ANSI or VT100) if the MUD requests one. This "
		  "option sets the terminal type that will be sent."), NULL);
	g_signal_connect(G_OBJECT(tmp), "changed", G_CALLBACK(prefs_entry_terminal_cb), (gpointer) prefs_window);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 5);

	tmp = gtk_label_new(_("MudList file:"));
	gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 10);

	tmp = gnome_file_entry_new(NULL, _("Select a MudList File..."));
	gtk_tooltips_set_tip (tooltip, gnome_file_entry_gtk_entry(GNOME_FILE_ENTRY(tmp)),
			_("Mudlist file to be used for the mudlist "
			  "functionality."), NULL);
	gtk_entry_set_text(GTK_ENTRY(gnome_file_entry_gtk_entry(GNOME_FILE_ENTRY(tmp))), prefs.MudListFile);
	gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, TRUE, 0);
	g_signal_connect(G_OBJECT(gnome_file_entry_gtk_entry(GNOME_FILE_ENTRY(tmp))), "changed",
					 G_CALLBACK(prefs_entry_mudlistfile_cb), (gpointer) prefs_window);

	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_NONE);
	tmp = gtk_label_new(_("Colour and Fonts"));
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), frame, tmp);

	vbox = prefs_color_frame(prefs_window);
	gtk_container_add(GTK_CONTAINER(frame), vbox);

	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_NONE);
	tmp = gtk_label_new(_("Appearance"));
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), frame, tmp);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(frame), vbox);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 5);

	tmp = gtk_label_new(_("Tabs are located:"));
	gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 10);

	tmp = gtk_menu_new();

	for (i = 0; i < 4; i++)
	{
		tmp2 = gtk_menu_item_new_with_label(l[i]);
		gtk_menu_shell_append(GTK_MENU_SHELL(tmp), tmp2);
	}

	tmp2 = gtk_option_menu_new();
	gtk_tooltips_set_tip (tooltip, tmp2,
			_("This settings defines where to place the "
			  "connection tabs that are used the change active "
			  "connection."), NULL);
	gtk_option_menu_set_menu(GTK_OPTION_MENU(tmp2), tmp);
	gtk_option_menu_set_history(GTK_OPTION_MENU(tmp2), tab_location_by_int(prefs.TabLocation));
	g_signal_connect(G_OBJECT(tmp2), "changed", G_CALLBACK(prefs_tab_location_changed_cb), NULL);
	gtk_box_pack_start(GTK_BOX(hbox), tmp2, FALSE, FALSE, 10);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 5);

	tmp = gtk_label_new(_("Scrollback:"));
	gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 10);

	spinner = (GtkAdjustment *) gtk_adjustment_new(prefs.Scrollback, 0, 5000, 1, 5, 5);
	tmp = gtk_spin_button_new(spinner, 1, 0);
	gtk_tooltips_set_tip (tooltip, tmp,
			_("Number of lines to save in the scrollback."), NULL);
	g_signal_connect(G_OBJECT(tmp), "value-changed", G_CALLBACK(prefs_scrollback_value_changed_cb), NULL);
	gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 10);

	tmp = gtk_label_new(_("lines"));
	gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 10);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 5);

	tmp = gtk_check_button_new_with_mnemonic(_("S_croll on output"));
	gtk_tooltips_set_tip (tooltip, tmp,
			_("If enabled, the terminal will scroll to the "
			  "bottom if new output appears in the connection "
			  "when the terminal was scrolled back."), NULL);
	gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(tmp), prefs.ScrollOnOutput);
	gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 10);
	g_signal_connect(G_OBJECT(tmp), "toggled", G_CALLBACK(prefs_scroll_output_changed_cb), NULL);

#ifndef WITHOUT_MAPPER
	/* AutoMapper settings */
	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_NONE);
	tmp = gtk_label_new(_("AutoMapper"));
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), frame, tmp);
	gtk_widget_show_all(notebook);
	if (data == 0x123456) // This means that we are called from map.c
	{
		gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), -1);
	}

	vbox = gtk_vbox_new(FALSE, 12);
	gtk_container_add(GTK_CONTAINER(frame), vbox);
	
	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 5);
		
	tmp = gtk_label_new(_("Unusual movement commands:"));
	gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 10);

	tmp = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 10);
	gtk_entry_set_text(GTK_ENTRY(tmp), gconf_client_get_string(gconf_client, "/apps/gnome-mud/mapper/unusual_exits", NULL));
	gtk_tooltips_set_tip(tooltip, tmp, 
			 _("If you use the automapper, you may want "
			   "to specify here some unusual movement "
			   "commands. When you use one of these, the "
			   "automapper will create a path to an other "
			   "map. Use a semicolon to separate the different "
			   "commands."), NULL);
	gtk_signal_connect(GTK_OBJECT(tmp), "changed", GTK_SIGNAL_FUNC(prefs_unusual_exits_cb), NULL);
#endif

	gtk_dialog_set_has_separator(GTK_DIALOG(prefs_window), FALSE);
	gtk_dialog_add_buttons(GTK_DIALOG(prefs_window), GTK_STOCK_HELP, GTK_RESPONSE_HELP, GTK_STOCK_CLOSE, GTK_RESPONSE_ACCEPT, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(prefs_window), GTK_RESPONSE_ACCEPT);
	g_signal_connect(G_OBJECT(prefs_window), "response", G_CALLBACK(profile_response_cb), NULL); 


	gtk_widget_show_all(prefs_window);
}

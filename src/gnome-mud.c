/* GNOME-Mud - A simple Mud CLient
 * Copyright (C) 1998-2001 Robin Ericsson <lobbin@localhost.nu>
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

#include "config.h"

#include <locale.h>
#include <gnome.h>
#include <pwd.h>
#include <signal.h>

#include "gnome-mud.h"

static char const rcsid[] =
    "$Id$";


int main (gint argc, char *argv[])
{
	gchar buf[500];
	
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
  
	gnome_init("gnome-mud", VERSION, argc, argv);  
  
	load_prefs   (); /* load preferences */
	load_profiles(); /* load connections and profiles */
	load_colors  (); /* load colors */
	init_window  ();
  
	g_snprintf(buf, 500, "%s/.gnome-mud/plugins/", g_get_home_dir());
	init_modules(buf);
	init_modules(PKGDATADIR);
  
	gtk_main();
	gnome_config_sync();

	gdk_exit (0);

	return 0;
}

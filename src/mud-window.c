/* GNOME-Mud - A simple Mud Client
 * mud-window.c
 * Copyright (C) 1998-2005 Robin Ericsson <lobbin@localhost.nu>
 * Copyright (C) 2005-2009 Les Harris <lharris@gnome.org>
 * Copyright (C) 2018 Mart Raudsepp <leio@gentoo.org>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gdk/gdk.h>
#include <vte/vte.h>
#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include <glib/gprintf.h>

#include "gnome-mud.h"
#include "gnome-mud-icons.h"
#include "mud-connection-view.h"
#include "mud-window-prefs.h"
#include "mud-window.h"
#include "mud-tray.h"
#include "mud-profile.h"
#include "mud-window-profile.h"
#include "mud-parse-base.h"
#include "mud-connections.h"
#include "mud-telnet.h"
#include "handlers/mud-telnet-handlers.h"
#include "utils.h"

struct _MudWindowPrivate
{
    GSList *mud_views_list;

    GtkWidget *notebook;
    GtkWidget *textview;
    GtkWidget *textviewscroll;
    GtkWidget *password_entry;

    GtkWidget *startlog;
    GtkWidget *stoplog;
    GtkWidget *bufferdump;

    GtkWidget *menu_close;
    GtkWidget *menu_reconnect;
    GtkWidget *menu_disconnect;
    GtkWidget *toolbar_disconnect;
    GtkWidget *toolbar_reconnect;

    GtkWidget *blank_label;
    MudConnectionView *current_view;

    GtkWidget *mi_profiles;

    GtkWidget *image;

    GSList *profile_menu_list;

    gint nr_of_tabs;
    gint textview_line_height;

    gint width;
    gint height;
};

/* Create the Type */
G_DEFINE_TYPE(MudWindow, mud_window, G_TYPE_OBJECT);

/* Property Identifiers */
enum
{
    PROP_MUD_WINDOW_0,
    PROP_WINDOW,
    PROP_TRAY
};

/* Signal Indices */
enum
{
    RESIZED,
    TOGGLEDOFF,
    LAST_SIGNAL
};

/* Signal Identifier Map */
static guint mud_window_signal[LAST_SIGNAL] = { 0, 0 };

/* Class Function Prototypes */
static void mud_window_init       (MudWindow *self);
static void mud_window_class_init (MudWindowClass *klass);
static void mud_window_finalize   (GObject *object);
static GObject *mud_window_constructor (GType gtype,
                                        guint n_properties,
                                        GObjectConstructParam *properties);
static void mud_window_set_property(GObject *object,
                                    guint prop_id,
                                    const GValue *value,
                                    GParamSpec *pspec);
static void mud_window_get_property(GObject *object,
                                    guint prop_id,
                                    GValue *value,
                                    GParamSpec *pspec);

/* Callback Prototypes */
static int mud_window_close(GtkWidget *widget, MudWindow *self);
static gboolean mud_window_grab_entry_focus_cb(GtkWidget *widget,
                               GdkEventFocus *event,
                               gpointer user_data);
static void mud_window_disconnect_cb(GtkWidget *widget, MudWindow *self);
static void mud_window_reconnect_cb(GtkWidget *widget, MudWindow *self);
static void mud_window_closewindow_cb(GtkWidget *widget, MudWindow *self);
static void mud_window_textview_buffer_changed(GtkTextBuffer *buffer, 
                                               MudWindow *self);
static gboolean mud_window_textview_keypress(GtkWidget *widget,
                                             GdkEventKey *event, 
                                             MudWindow *self);
static gboolean mud_window_entry_keypress(GtkWidget *widget,
                                          GdkEventKey *event, 
                                          MudWindow *self);
static void mud_window_notebook_page_change(GtkNotebook *notebook,
                                            gpointer page,
                                            gint arg,
                                            MudWindow *self);
static void mud_window_preferences_cb(GtkWidget *widget, MudWindow *self);
static void mud_window_profiles_cb(GtkWidget *widget, MudWindow *self);
static void mud_window_about_cb(GtkWidget *widget, MudWindow *self);
static void mud_window_mconnect_dialog(GtkWidget *widget, MudWindow *self);

static gboolean mud_window_configure_event(GtkWidget *widget,
                                           GdkEventConfigure *event,
                                           gpointer user_data);
static void mud_window_buffer_cb(GtkWidget *widget, MudWindow *self);
static void mud_window_select_profile(GtkWidget *widget, MudWindow *self);
static void mud_window_profile_menu_set_cb(GtkWidget *widget, gpointer user_data);
static void mud_window_startlog_cb(GtkWidget *widget, MudWindow *self);
static void mud_window_stoplog_cb(GtkWidget *widget, MudWindow *self);
static void mud_window_size_allocate_cb(GtkWidget *widget,
                                        GtkAllocation *allocation,
                                        MudWindow *self);

/* Private Method Prototypes */
static void mud_window_remove_connection_view(MudWindow *self, gint nr);
static gint mud_window_textview_get_display_line_count(GtkTextView *textview);
static void mud_window_textview_ensure_height(MudWindow *self, guint max_lines);
static void mud_window_clear_profiles_menu(GtkWidget *widget, gpointer data);

/* Class Functions */
static void
mud_window_class_init (MudWindowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    /* Override base object constructor */
    object_class->constructor = mud_window_constructor;

    /* Override parent's finalize */
    object_class->finalize = mud_window_finalize;

    /* Override base object property methods */
    object_class->set_property = mud_window_set_property;
    object_class->get_property = mud_window_get_property;

    /* Add private data to class */
    g_type_class_add_private(klass, sizeof(MudWindowPrivate));

    /* Create and Install Properties */
    g_object_class_install_property(object_class,
            PROP_WINDOW,
            g_param_spec_object("window",
                "gtk window",
                "the gtk window of mud window",
                GTK_TYPE_WINDOW,
                G_PARAM_READABLE));

    g_object_class_install_property(object_class,
            PROP_TRAY,
            g_param_spec_object("tray",
                "mud tray",
                "mud status tray icon",
                MUD_TYPE_TRAY,
                G_PARAM_READABLE));
    
    /* Register Signals */
    mud_window_signal[RESIZED] =
        g_signal_new("resized",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST | G_SIGNAL_NO_HOOKS,
                     0,
                     NULL,
                     NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE,
                     0);

    mud_window_signal[TOGGLEDOFF] =
        g_signal_new("toggled-off",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST | G_SIGNAL_NO_HOOKS,
                     0,
                     NULL,
                     NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE,
                     0);

}

static void
mud_window_init (MudWindow *self)
{
    GtkBuilder *builder;
    GError *error = NULL;
    GtkTextIter iter;
    gint y;

    /* Get our private data */
    self->priv = MUD_WINDOW_GET_PRIVATE(self);

    /* start glading */
    builder = gtk_builder_new();
    if(gtk_builder_add_from_file(builder, UIDIR "/main.ui", &error) == 0)
        g_error("Failed to load: %s", error->message);

    /* set public properties */
    self->window = GTK_WINDOW(gtk_builder_get_object(builder, "main_window"));
    self->tray = g_object_new(MUD_TYPE_TRAY,
                              "parent-window", self->window,
                              NULL); 

    /* set private members */
    self->priv->nr_of_tabs = 0;
    self->priv->current_view = NULL;
    self->priv->mud_views_list = NULL;
    self->priv->profile_menu_list = NULL;
    self->priv->menu_disconnect = GTK_WIDGET(gtk_builder_get_object(builder, "menu_disconnect"));
    self->priv->toolbar_disconnect = GTK_WIDGET(gtk_builder_get_object(builder, "toolbar_disconnect"));
    self->priv->menu_reconnect = GTK_WIDGET(gtk_builder_get_object(builder, "menu_reconnect"));
    self->priv->toolbar_reconnect = GTK_WIDGET(gtk_builder_get_object(builder, "toolbar_reconnect"));
    self->priv->menu_close = GTK_WIDGET(gtk_builder_get_object(builder, "menu_closewindow"));
    self->priv->startlog = GTK_WIDGET(gtk_builder_get_object(builder, "menu_start_logging"));
    self->priv->stoplog = GTK_WIDGET(gtk_builder_get_object(builder, "menu_stop_logging"));
    self->priv->bufferdump = GTK_WIDGET(gtk_builder_get_object(builder, "menu_dump_buffer"));
    self->priv->mi_profiles = GTK_WIDGET(gtk_builder_get_object(builder, "mi_profiles_menu"));
    self->priv->notebook = GTK_WIDGET(gtk_builder_get_object(builder, "notebook"));
    self->priv->textviewscroll = GTK_WIDGET(gtk_builder_get_object(builder, "text_view_scroll"));
    self->priv->textview = GTK_WIDGET(gtk_builder_get_object(builder, "text_view"));
    self->priv->image = GTK_WIDGET(gtk_builder_get_object(builder, "image"));
    self->priv->password_entry = GTK_WIDGET(gtk_builder_get_object(builder, "password_entry"));

    /* connect quit buttons */
    g_signal_connect(self->window,
                     "destroy",
                     G_CALLBACK(mud_window_close),
                     self);

    g_signal_connect(GTK_WIDGET(gtk_builder_get_object(builder, "menu_quit")),
                     "activate",
                     G_CALLBACK(mud_window_close),
                     self);

    /* connect connect buttons */
    g_signal_connect(GTK_WIDGET(gtk_builder_get_object(builder, "main_connect")),
                     "activate",
                     G_CALLBACK(mud_window_mconnect_dialog),
                     self);

    g_signal_connect(GTK_WIDGET(gtk_builder_get_object(builder, "toolbar_connect")),
                     "clicked",
                     G_CALLBACK(mud_window_mconnect_dialog),
                     self);

    /* connect disconnect buttons */
    g_signal_connect(self->priv->menu_disconnect,
                     "activate",
                     G_CALLBACK(mud_window_disconnect_cb),
                     self);

    g_signal_connect(self->priv->toolbar_disconnect,
                     "clicked",
                     G_CALLBACK(mud_window_disconnect_cb),
                     self);

    /* connect reconnect buttons */
    g_signal_connect(self->priv->menu_reconnect,
                     "activate",
                     G_CALLBACK(mud_window_reconnect_cb),
                     self);

    g_signal_connect(self->priv->toolbar_reconnect,
                     "clicked",
                     G_CALLBACK(mud_window_reconnect_cb),
                     self);

    /* connect close window button */
    g_signal_connect(self->priv->menu_close,
                     "activate",
                     G_CALLBACK(mud_window_closewindow_cb),
                     self);

    /* logging */
    g_signal_connect(self->priv->startlog,
                     "activate",
                     G_CALLBACK(mud_window_startlog_cb),
                     self);

    g_signal_connect(self->priv->stoplog,
                     "activate", G_CALLBACK(mud_window_stoplog_cb),
                     self);

    g_signal_connect(self->priv->bufferdump,
                     "activate",
                     G_CALLBACK(mud_window_buffer_cb),
                     self);

    /* preferences window button */
    g_signal_connect(GTK_WIDGET(gtk_builder_get_object(builder, "menu_preferences")),
                     "activate",
                     G_CALLBACK(mud_window_preferences_cb),
                     self);

    g_signal_connect(GTK_WIDGET(gtk_builder_get_object(builder, "menu_about")),
                     "activate",
                     G_CALLBACK(mud_window_about_cb),
                     self);

    /* other objects */
    g_signal_connect(self->priv->notebook,
                     "switch-page",
                     G_CALLBACK(mud_window_notebook_page_change),
                     self);

    g_signal_connect(self->window,
                     "configure-event",
                     G_CALLBACK(mud_window_configure_event),
                     self);

    g_signal_connect(self->window,
                     "size-allocate",
                     G_CALLBACK(mud_window_size_allocate_cb),
                     self);

    g_signal_connect(self->priv->textview,
                     "key_press_event",
                     G_CALLBACK(mud_window_textview_keypress),
                     self);

    g_signal_connect(self->priv->password_entry,
                     "key_press_event",
                     G_CALLBACK(mud_window_entry_keypress),
                     self);

    g_signal_connect(
            gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->priv->textview)),
            "changed",
            G_CALLBACK(mud_window_textview_buffer_changed),
            self);

    /* Setup TextView */
    gtk_text_view_set_wrap_mode(
            GTK_TEXT_VIEW(self->priv->textview), GTK_WRAP_WORD_CHAR);

    /* Set the initial height of the input box equal to the height of one line */
    gtk_text_buffer_get_start_iter(
            gtk_text_view_get_buffer(
                GTK_TEXT_VIEW(self->priv->textview)),
                &iter);

    gtk_text_view_get_line_yrange(GTK_TEXT_VIEW(self->priv->textview),
                                  &iter, &y,
                                  &self->priv->textview_line_height);

    gtk_widget_set_size_request(self->priv->textview, -1,
                                self->priv->textview_line_height*1);
    gtk_widget_set_size_request(
            GTK_SCROLLED_WINDOW(self->priv->textviewscroll)->vscrollbar,
            -1, 1);

    if (gtk_widget_get_visible(self->priv->textviewscroll))
        gtk_widget_queue_resize(self->priv->textviewscroll);

    gtk_window_get_size(GTK_WINDOW(self->window),
                        &self->priv->width,
                        &self->priv->height);

    gtk_widget_set_visible(GTK_WIDGET(self->window), TRUE);

    g_object_unref(builder);
}

static GObject *
mud_window_constructor (GType gtype,
                        guint n_properties,
                        GObjectConstructParam *properties)
{
    MudWindow *self;
    GObject *obj;
    MudWindowClass *klass;
    GObjectClass *parent_class;

    /* Chain up to parent constructor */
    klass = MUD_WINDOW_CLASS( g_type_class_peek(MUD_TYPE_WINDOW) );
    parent_class = G_OBJECT_CLASS( g_type_class_peek_parent(klass) );
    obj = parent_class->constructor(gtype, n_properties, properties);

    self = MUD_WINDOW(obj);

    self->profile_manager = g_object_new(MUD_TYPE_PROFILE_MANAGER,
                                         "parent-window", self,
                                        NULL);

    mud_window_populate_profiles_menu(self);

    return obj;
}

static void
mud_window_finalize (GObject *object)
{

    GSList *entry;
    MudWindow    *self;
    GObjectClass *parent_class;

    self = MUD_WINDOW(object);

    entry = self->priv->mud_views_list;

    while(entry != NULL)
    {
        g_object_unref(entry->data);

        entry = g_slist_next(entry);
    }

    g_slist_free(self->priv->mud_views_list);
    
    g_object_unref(self->tray);

    g_object_unref(self->profile_manager);

    parent_class = g_type_class_peek_parent(G_OBJECT_GET_CLASS(object));
    parent_class->finalize(object);

    gtk_main_quit();
}

static void
mud_window_set_property(GObject *object,
                        guint prop_id,
                        const GValue *value,
                        GParamSpec *pspec)
{
    switch(prop_id)
    {
        /* These properties aren't writeable. If we get here
         * something is really screwy. */
        case PROP_WINDOW:
            g_return_if_reached();
            break;

        case PROP_TRAY:
            g_return_if_reached();
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void
mud_window_get_property(GObject *object,
                        guint prop_id,
                        GValue *value,
                        GParamSpec *pspec)
{
    MudWindow *self;

    self = MUD_WINDOW(object);

    switch(prop_id)
    {
        case PROP_WINDOW:
            g_value_take_object(value, self->window);
            break;

        case PROP_TRAY:
            g_value_take_object(value, self->tray);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

/* Callbacks */
static int
mud_window_close(GtkWidget *widget, MudWindow *self)
{
    g_object_unref(self);
    return TRUE;
}

static gboolean
mud_window_grab_entry_focus_cb(GtkWidget *widget,
                               GdkEventFocus *event,
                               gpointer user_data)
{
    MudWindow *self = MUD_WINDOW(user_data);

    if(gtk_widget_get_visible(self->priv->textview))
        gtk_widget_grab_focus(self->priv->textview);
    else
        gtk_widget_grab_focus(self->priv->password_entry);

    return TRUE;
}

static void
mud_window_disconnect_cb(GtkWidget *widget, MudWindow *self)
{
    if (self->priv->current_view != NULL)
    {
        gtk_widget_set_sensitive(self->priv->startlog, FALSE);
        gtk_widget_set_sensitive(self->priv->menu_disconnect, FALSE);
        gtk_widget_set_sensitive(self->priv->toolbar_disconnect, FALSE);
        mud_connection_view_disconnect(self->priv->current_view);
    }
}

static void
mud_window_reconnect_cb(GtkWidget *widget, MudWindow *self)
{
    if (self->priv->current_view != NULL)
    {
        gtk_widget_set_sensitive(self->priv->startlog, TRUE);
        gtk_widget_set_sensitive(self->priv->menu_reconnect, FALSE);
        gtk_widget_set_sensitive(self->priv->menu_disconnect, TRUE);
        gtk_widget_set_sensitive(self->priv->toolbar_reconnect, FALSE);
        gtk_widget_set_sensitive(self->priv->toolbar_disconnect, TRUE);
        mud_connection_view_reconnect(self->priv->current_view);
    }
}

static void
mud_window_closewindow_cb(GtkWidget *widget, MudWindow *self)
{
    mud_window_close_current_window(self);
}

static void
mud_window_textview_buffer_changed(GtkTextBuffer *buffer, MudWindow *self)
{
    mud_window_textview_ensure_height(self, 5);
}

static gboolean
mud_window_textview_keypress(GtkWidget *widget, GdkEventKey *event, MudWindow *self)
{
    gchar *text;
    const gchar *history;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->priv->textview));
    GtkTextIter start, end;
    MudParseBase *base;

    if ((event->keyval == GDK_Return || event->keyval == GDK_KP_Enter) &&
            (event->state & gtk_accelerator_get_default_mod_mask()) == 0)
    {
        gtk_text_buffer_get_bounds(buffer, &start, &end);

        if (self->priv->current_view)
        {
            text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

            if (g_str_equal(text, ""))
                text = g_strdup(" ");

            g_object_get(self->priv->current_view,
                        "parse-base", &base,
                        NULL);

            if(mud_parse_base_do_aliases(base, text))
                mud_connection_view_send(self->priv->current_view, text);

            g_free(text);
        }

        if (g_settings_get_boolean(self->priv->current_view->profile->settings, "keep-text"))
            gtk_text_buffer_select_range(buffer, &start, &end);
        else
            gtk_text_buffer_delete(buffer, &start, &end);

        return TRUE;
    }

    if(self->priv->current_view)
    {
        if(event->keyval == GDK_Up)
        {
            history = 
                mud_connection_view_get_history_item(self->priv->current_view, 
                                                     HISTORY_UP);

            if(history)
            {
                gtk_text_buffer_set_text(buffer, history, strlen(history));
                gtk_text_buffer_get_bounds(buffer, &start, &end);
                gtk_text_buffer_select_range(buffer, &start, &end);
            }

            return TRUE;
        }

        if(event->keyval == GDK_Down)
        {
            history =
                mud_connection_view_get_history_item(self->priv->current_view,
                                                     HISTORY_DOWN);

            if(history)
            {
                gtk_text_buffer_set_text(buffer, history, strlen(history));
                gtk_text_buffer_get_bounds(buffer, &start, &end);
                gtk_text_buffer_select_range(buffer, &start, &end);
            }

            return TRUE;
        }
    }

    return FALSE;
}

static gboolean
mud_window_entry_keypress(GtkWidget *widget,
                          GdkEventKey *event,
                          MudWindow *self)
{
    const gchar *text;

    if ((event->keyval == GDK_Return || event->keyval == GDK_KP_Enter) &&
            (event->state & gtk_accelerator_get_default_mod_mask()) == 0)
    {
        if (self->priv->current_view)
        {
            text = gtk_entry_get_text(GTK_ENTRY(self->priv->password_entry));

            mud_connection_view_send(self->priv->current_view, text);

            gtk_entry_set_text(GTK_ENTRY(self->priv->password_entry), "");
        }

        return TRUE;
    }

    return FALSE;
}

static void
mud_window_notebook_page_change(GtkNotebook *notebook, gpointer page, gint arg, MudWindow *self)
{
    gboolean connected;
    gboolean logging;
    GtkWidget *box;
    GtkWidget *viewport;
    GList *list = NULL;
    GtkImage *image;

    if(IS_MUD_CONNECTION_VIEW(self->priv->current_view))
        mud_connection_view_hide_subwindows(self->priv->current_view);

    self->priv->current_view =
        g_object_get_data(
                G_OBJECT(gtk_notebook_get_nth_page(notebook, arg)),
                "connection-view");

    if(IS_MUD_CONNECTION_VIEW(self->priv->current_view))
        mud_connection_view_show_subwindows(self->priv->current_view);

    if (self->priv->nr_of_tabs != 0)
    {
        mud_window_profile_menu_set_active(self,
                                           self->priv->current_view->profile);

        g_object_get(self->priv->current_view,
                     "connected", &connected,
                     "logging", &logging,
                     NULL);
        
        if(logging)
        {
            gtk_widget_set_sensitive(self->priv->startlog, FALSE);
            gtk_widget_set_sensitive(self->priv->stoplog, TRUE);
        }
        else
        {
            gtk_widget_set_sensitive(self->priv->startlog, TRUE);
            gtk_widget_set_sensitive(self->priv->stoplog, FALSE);
        }

        if(!connected)
        {
            gtk_widget_set_sensitive(self->priv->startlog, FALSE);
            gtk_widget_set_sensitive(self->priv->stoplog, FALSE);
        }

        gtk_widget_set_sensitive(self->priv->menu_disconnect, connected);
        gtk_widget_set_sensitive(self->priv->toolbar_disconnect, connected);
        gtk_widget_set_sensitive(self->priv->toolbar_reconnect, !connected);

        mud_window_toggle_input_mode(self, self->priv->current_view);

        if(gtk_widget_get_visible(self->priv->textview))
            gtk_widget_grab_focus(self->priv->textview);
        else
            gtk_widget_grab_focus(self->priv->password_entry);

        g_object_get(self->priv->current_view,
                "ui-vbox", &viewport,
                NULL);

        box = gtk_notebook_get_tab_label(GTK_NOTEBOOK(self->priv->notebook),
                viewport);
        list = gtk_container_get_children(GTK_CONTAINER(box));
        image = GTK_IMAGE(list->data);

        g_list_free(list);

        gtk_image_set_from_icon_name(image, GMUD_STOCK_NEGATIVE, GTK_ICON_SIZE_MENU);
    }
    else
    {
        gtk_widget_set_sensitive(self->priv->startlog, FALSE);
        gtk_widget_set_sensitive(self->priv->stoplog, FALSE);
        gtk_widget_set_sensitive(self->priv->bufferdump, FALSE);
        gtk_widget_set_sensitive(self->priv->menu_close, FALSE);
        gtk_widget_set_sensitive(self->priv->menu_reconnect, FALSE);
        gtk_widget_set_sensitive(self->priv->menu_disconnect, FALSE);
        gtk_widget_set_sensitive(self->priv->toolbar_disconnect, FALSE);
        gtk_widget_set_sensitive(self->priv->toolbar_reconnect, FALSE);
    }
}

static void
mud_window_preferences_cb(GtkWidget *widget, MudWindow *self)
{
  /* TODO: Open default profile if no current_view? */
  if (self->priv->current_view == NULL)
    return;

  g_object_new(MUD_TYPE_WINDOW_PREFS,
               "parent-window", self,
               "mud-profile", self->priv->current_view->profile, /* TODO: Bind instead and handle live profile changes properly? */
               NULL);
}

static void
mud_window_profiles_cb(GtkWidget *widget, MudWindow *self)
{
    g_object_new(MUD_TYPE_PROFILE_WINDOW,
            "parent-window", self,
            NULL);
}

static void
mud_window_about_cb(GtkWidget *widget, MudWindow *self)
{
    static const gchar * const authors[] = {
        "Robin Ericsson <lobbin@localhost.nu>",
        "Jordi Mallach <jordi@sindominio.net>",
        "Daniel Patton <seven-nation@army.com>",
        "Les Harris <lharris@gnome.org>",
        "Mart Raudsepp <leio@gentoo.org>",
        NULL
    };

    static const gchar * const documenters[] = {
        "Jordi Mallach <jordi@sindominio.net>",
        "Petter E. Stokke <gibreel@project23.no>",
        NULL
    };

    static const gchar * const artists[] = {
        "Daniel Taylor <DanTaylor@web.de>",
        "Andreas Nilsson <andreasn@gnome.org>",
        NULL
    };

    static const gchar copyright[] = "Copyright \xc2\xa9 1998-2005 Robin Ericsson\n"
                                     "Copyright \xc2\xa9 2005-2009 Les Harris";

    static const gchar comments[] = N_("A Multi-User Dungeon (MUD) client for GNOME");

    GdkPixbuf *logo = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(), "gnome-mud",
            128, GTK_ICON_LOOKUP_FORCE_SVG, NULL);

    gtk_show_about_dialog(GTK_WINDOW(self->window),
            "artists", artists,
            "authors", authors,
            "comments", _(comments),
            "copyright", copyright,
            "documenters", documenters,
            "logo", logo,
            "translator-credits", _("translator-credits"),
            "version", VERSION,
            "website", "http://live.gnome.org/GnomeMud",
            "name", "Gnome-Mud",
            NULL);

    if(logo)
        g_object_unref(logo);
}

static void
mud_window_mconnect_dialog(GtkWidget *widget, MudWindow *self)
{
    g_object_new(MUD_TYPE_CONNECTIONS,
            "parent-window", self,
            NULL);
}

static gboolean
mud_window_configure_event(GtkWidget *widget, GdkEventConfigure *event, gpointer user_data)
{
    MudWindow *self = MUD_WINDOW(user_data);

    if (self->priv->nr_of_tabs == 0)
    {
        GError *err = NULL;
        GdkPixbuf *buf;

        buf = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(),
                                       GMUD_STOCK_ICON,
                                       event->width >> 1,
                                       GTK_ICON_LOOKUP_FORCE_SVG,
                                       &err);

        gtk_image_set_from_pixbuf(GTK_IMAGE(self->priv->image), buf);

        g_object_unref(buf);
    }

    if(gtk_widget_get_visible(self->priv->textview))
        gtk_widget_grab_focus(self->priv->textview);
    else
        gtk_widget_grab_focus(self->priv->password_entry);

    return FALSE;
}

static void
mud_window_size_allocate_cb(GtkWidget *widget,
                            GtkAllocation *allocation,
                            MudWindow *self)
{
    if(gtk_widget_get_mapped(GTK_WIDGET(self->window)))
    {
        if(self->priv->width != allocation->width ||
           self->priv->height != allocation->height)
        {
            self->priv->width = allocation->width;
            self->priv->height = allocation->height;

            g_signal_emit(self, mud_window_signal[RESIZED], 0);
        }
    }
}

static void
mud_window_buffer_cb(GtkWidget *widget, MudWindow *self)
{
    GtkBuilder *builder;
    GError *error = NULL;
    GtkWidget *dialog;
    gint result;

    builder = gtk_builder_new();
    if(gtk_builder_add_from_file(builder, UIDIR "/main.ui", &error) == 0)
        g_error("Failed to load: %s", error->message);

    dialog = GTK_WIDGET(gtk_builder_get_object(builder, "save_dialog"));

    gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), "buffer.txt");

    result = gtk_dialog_run(GTK_DIALOG(dialog));

    if(result == GTK_RESPONSE_OK)
    {
        gchar *filename;
        gchar *buffer_text;
        VteTerminal *term;
        GtkClipboard *clipboard;
        GError *err = NULL;

        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

        g_object_get(self->priv->current_view,
                "terminal", &term,
                NULL);

        /* This is really hackish but the only alternative,
         * vte_terminal_get_text_range, is just broken. */

        clipboard = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
        vte_terminal_select_all(term);
        vte_terminal_copy_primary(term);
        vte_terminal_select_none(term);
        buffer_text = gtk_clipboard_wait_for_text(clipboard);

        if(buffer_text)
            if(!g_file_set_contents(filename, buffer_text, -1, &err))
                utils_error_message(GTK_WIDGET(self->window),
                                    _("Error Saving Buffer"),
                                    "%s",
                                    err->message);
        if(buffer_text)
            g_free(buffer_text);
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
    g_object_unref(builder);
}

static void
mud_window_select_profile(GtkWidget *widget, MudWindow *self)
{
    MudProfile *profile;
    GtkWidget *profileLabel;

    profileLabel = gtk_bin_get_child(GTK_BIN(widget));

    if (self->priv->current_view)
    {
        profile = g_object_get_data (G_OBJECT (profileLabel), "profile-object");
        if (profile)
            mud_connection_view_set_profile(MUD_CONNECTION_VIEW(self->priv->current_view), profile);
    }
}

static void
mud_window_profile_menu_set_cb(GtkWidget *widget,
                               gpointer   user_data)
{
  GtkWidget *label;
  MudProfile *profile = MUD_PROFILE (user_data);

  label = gtk_bin_get_child (GTK_BIN (widget));

  if (GTK_IS_LABEL (label) && g_object_get_data (G_OBJECT (label), "profile-object") == profile)
    {
      gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (widget), TRUE);
    }
}

static void
mud_window_startlog_cb(GtkWidget *widget, MudWindow *self)
{
    MudLog *log;
    mud_connection_view_start_logging(MUD_CONNECTION_VIEW(self->priv->current_view));

    g_object_get(self->priv->current_view, "log", &log, NULL);

    if(mud_log_islogging(log))
    {
        gtk_widget_set_sensitive(self->priv->startlog, FALSE);
        gtk_widget_set_sensitive(self->priv->stoplog, TRUE);
    }
}

static void
mud_window_stoplog_cb(GtkWidget *widget, MudWindow *self)
{
    mud_connection_view_stop_logging(MUD_CONNECTION_VIEW(self->priv->current_view));
    gtk_widget_set_sensitive(self->priv->stoplog, FALSE);
    gtk_widget_set_sensitive(self->priv->startlog, TRUE);
}

/* Private Methods */
static void
mud_window_remove_connection_view(MudWindow *self, gint nr)
{
    self->priv->mud_views_list =
        g_slist_remove(self->priv->mud_views_list, self->priv->current_view);

    g_object_unref(self->priv->current_view);
    gtk_notebook_remove_page(GTK_NOTEBOOK(self->priv->notebook), nr);

    if (--self->priv->nr_of_tabs < 2)
        gtk_notebook_set_show_tabs(GTK_NOTEBOOK(self->priv->notebook), FALSE);

    if (self->priv->nr_of_tabs == 0)
    {
        gint w, h;
        GdkPixbuf *buf;
        GError *err = NULL;

        gtk_window_get_size(GTK_WINDOW(self->window), &w, &h);

        if(self->priv->image)
            g_object_unref(self->priv->image);

        buf = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(),
                "gnome-mud", w >> 1, GTK_ICON_LOOKUP_FORCE_SVG, &err);

        self->priv->image = gtk_image_new_from_pixbuf(buf);
        gtk_widget_show(self->priv->image);

        if(buf)
            g_object_unref(buf);

        gtk_notebook_append_page(GTK_NOTEBOOK(self->priv->notebook), self->priv->image, NULL);
    }
}

static gint
mud_window_textview_get_display_line_count(GtkTextView *textview)
{
    gint result = 1;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(textview);
    GtkTextIter iter;

    gtk_text_buffer_get_start_iter(buffer, &iter);
    while (gtk_text_view_forward_display_line(textview, &iter))
        ++result;

    if (gtk_text_buffer_get_line_count(buffer) != 1)
    {
        GtkTextIter iter2;
        gtk_text_buffer_get_end_iter(buffer, &iter2);
        if (gtk_text_iter_get_chars_in_line(&iter) == 0)
            ++result;
    }

    return result;
}

static void
mud_window_textview_ensure_height(MudWindow *self, guint max_lines)
{
    gint lines = mud_window_textview_get_display_line_count(GTK_TEXT_VIEW(self->priv->textview));
    gtk_widget_set_size_request(self->priv->textview, -1,
            self->priv->textview_line_height * MIN(lines, max_lines));
    gtk_widget_queue_resize(gtk_widget_get_parent(self->priv->textview));
}


static void
mud_window_clear_profiles_menu(GtkWidget *widget, gpointer data)
{
    gtk_widget_destroy(widget);
}

/* Public Methods */
const GSList *
mud_window_get_views(MudWindow *window)
{
    return window->priv->mud_views_list;
}

void
mud_window_toggle_input_mode(MudWindow *self,
                             MudConnectionView *view)
{
    gboolean local_echo;

    g_return_if_fail(IS_MUD_WINDOW(self));

    /* Don't want to log a critical when a view unrefs */
    if(!IS_MUD_CONNECTION_VIEW(view))
        return;

    if(g_direct_equal(self->priv->current_view, view))
    {
        g_object_get(view, "local-echo", &local_echo, NULL);

        if(local_echo)
        {
            if(gtk_widget_get_mapped(self->priv->password_entry))
            {
                gtk_widget_hide(self->priv->password_entry);

                gtk_widget_show(self->priv->textviewscroll);
                gtk_widget_show(self->priv->textview);

                gtk_widget_grab_focus(self->priv->textview);

                g_signal_emit(self, mud_window_signal[TOGGLEDOFF], 0);
            }
        }
        else
        {
            gtk_widget_show(self->priv->password_entry);

            gtk_widget_hide(self->priv->textviewscroll);
            gtk_widget_hide(self->priv->textview);

            gtk_widget_grab_focus(self->priv->password_entry);
        }
    }
}

void
mud_window_toggle_tab_icon(MudWindow *self,
                           MudConnectionView *view)
{
    g_return_if_fail(IS_MUD_WINDOW(self));

    if(!IS_MUD_CONNECTION_VIEW(view))
        return;

    if(!g_direct_equal(self->priv->current_view, view))
    {
        GtkWidget *box;
        GtkWidget *viewport;
        GList *list = NULL;
        GtkImage *image;

        g_object_get(view,
                     "ui-vbox", &viewport,
                     NULL);

        box = gtk_notebook_get_tab_label(GTK_NOTEBOOK(self->priv->notebook),
                                         viewport);
        list = gtk_container_get_children(GTK_CONTAINER(box));
        image = GTK_IMAGE(list->data);

        g_list_free(list);

        gtk_image_set_from_icon_name(image, GMUD_STOCK_POSITIVE, GTK_ICON_SIZE_MENU);
    }
}

void
mud_window_close_current_window(MudWindow *self)
{
    g_return_if_fail(IS_MUD_WINDOW(self));

    if (self->priv->nr_of_tabs > 0)
    {
        gint nr = gtk_notebook_get_current_page(GTK_NOTEBOOK(self->priv->notebook));

        mud_window_remove_connection_view(self, nr);

        if(self->priv->nr_of_tabs == 0)
            mud_tray_update_icon(MUD_TRAY(self->tray), offline_connecting);
    }
}

void
mud_window_profile_menu_set_active (MudWindow *self, MudProfile *profile)
{
  g_return_if_fail (IS_MUD_WINDOW(self));

  gtk_container_foreach (GTK_CONTAINER (self->priv->mi_profiles),
                         mud_window_profile_menu_set_cb,
                         profile);
}

static void
add_profile_to_menu (MudProfile *profile,
                     MudWindow  *self)
{
  const gchar *profile_name;
  GtkWidget *profile_item;

  profile_name = mud_profile_get_name (profile);
  profile_item = gtk_radio_menu_item_new_with_label (self->priv->profile_menu_list,
                                                     profile_name);
  g_object_set_data (G_OBJECT (profile_item), "profile-object", profile);
  gtk_widget_show (profile_item);
  self->priv->profile_menu_list = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (profile_item));

  gtk_menu_shell_append (GTK_MENU_SHELL (self->priv->mi_profiles), profile_item);

  g_signal_connect (profile_item,
                    "activate",
                    G_CALLBACK (mud_window_select_profile),
                    self);
}

void
mud_window_populate_profiles_menu(MudWindow *self)
{
    GSequence *profiles;
    GtkWidget *sep;
    GtkWidget *manage;
    GtkWidget *icon;

    g_return_if_fail(IS_MUD_WINDOW(self));

    self->priv->profile_menu_list = NULL;

    profiles = mud_profile_manager_get_profiles (self->profile_manager);

    gtk_container_foreach(GTK_CONTAINER(self->priv->mi_profiles),
                          mud_window_clear_profiles_menu,
                          NULL);

    g_sequence_foreach (profiles, (GFunc)add_profile_to_menu, self); /* TODO: Move back to iterating here in a loop? */

    sep = gtk_separator_menu_item_new();
    gtk_widget_show(sep);
    gtk_menu_shell_append(GTK_MENU_SHELL(self->priv->mi_profiles), sep);

    icon = gtk_image_new_from_stock("gtk-edit", GTK_ICON_SIZE_MENU);

    manage = gtk_image_menu_item_new_with_mnemonic(_("_Manage Profiles…"));
    gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(manage), icon);
    gtk_widget_show(manage);

    gtk_menu_shell_append(GTK_MENU_SHELL(self->priv->mi_profiles), manage);

    g_signal_connect(manage,
                     "activate",
                     G_CALLBACK(mud_window_profiles_cb),
                     self);

    if(self->priv->current_view)
        mud_window_profile_menu_set_active(self, self->priv->current_view->profile);
    /* TODO: Show default profile as the active one if no current_view (yet)? Or least make the direct "Edit profile" not chooseable until there is no associated view */
}
    
void
mud_window_add_connection_view(MudWindow *self, GObject *cview, gchar *tabLbl)
{
    gint nr;
    VteTerminal *terminal;
    GtkVBox *viewport;
    GtkHBox *hbox;
    GtkWidget *tab_label;
    GtkImage *image, *close_image;
    GtkButton *close_button;
    MudConnectionView *view = MUD_CONNECTION_VIEW(cview);

    g_return_if_fail(IS_MUD_WINDOW(self));
    g_return_if_fail(IS_MUD_CONNECTION_VIEW(view));

    if (self->priv->nr_of_tabs++ == 0)
    {
        gtk_notebook_remove_page(GTK_NOTEBOOK(self->priv->notebook), 0);
        self->priv->image = NULL;
    }

    g_object_get(view,
                 "ui-vbox", &viewport,
                 "terminal", &terminal,
                 NULL);

    tab_label = gtk_label_new(tabLbl);
    hbox = GTK_HBOX(gtk_hbox_new(FALSE, 0));
    image = GTK_IMAGE(gtk_image_new_from_icon_name(GMUD_STOCK_NEGATIVE,
                                                   GTK_ICON_SIZE_MENU));

    close_image = GTK_IMAGE(gtk_image_new_from_icon_name("window-close",
                                                         GTK_ICON_SIZE_MENU));
    close_button = GTK_BUTTON(gtk_button_new());
    gtk_button_set_relief(close_button, GTK_RELIEF_NONE);
    gtk_button_set_image(close_button, GTK_WIDGET(close_image));

    gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(image), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), tab_label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(close_button), FALSE, FALSE, 0);

    gtk_widget_show_all(GTK_WIDGET(hbox));

    nr = gtk_notebook_append_page(GTK_NOTEBOOK(self->priv->notebook),
                                  GTK_WIDGET(viewport),
                                  GTK_WIDGET(hbox));

    gtk_notebook_set_tab_label_packing(GTK_NOTEBOOK(self->priv->notebook),
                                       GTK_WIDGET(viewport),
                                       TRUE,
                                       TRUE,
                                       GTK_PACK_START);

    gtk_notebook_set_current_page(GTK_NOTEBOOK(self->priv->notebook), nr);

    gtk_widget_set_sensitive(self->priv->startlog, TRUE);
    gtk_widget_set_sensitive(self->priv->bufferdump, TRUE);
    gtk_widget_set_sensitive(self->priv->menu_close, TRUE);
    gtk_widget_set_sensitive(self->priv->menu_reconnect, FALSE);
    gtk_widget_set_sensitive(self->priv->menu_disconnect, TRUE);
    gtk_widget_set_sensitive(self->priv->toolbar_disconnect, TRUE);
    gtk_widget_set_sensitive(self->priv->toolbar_reconnect, FALSE);

    g_signal_connect(terminal,
                     "focus-in-event",
                     G_CALLBACK(mud_window_grab_entry_focus_cb),
                     self);

    g_signal_connect(close_button,
                     "clicked",
                     G_CALLBACK(mud_window_closewindow_cb),
                     self);

    self->priv->mud_views_list = g_slist_append(self->priv->mud_views_list, view);

    if (self->priv->nr_of_tabs > 1)
    {
        gtk_notebook_set_show_tabs(GTK_NOTEBOOK(self->priv->notebook), TRUE);
    }
}

void
mud_window_disconnected(MudWindow *self)
{
    g_return_if_fail(IS_MUD_WINDOW(self));

    gtk_widget_set_sensitive(self->priv->startlog, FALSE);
    gtk_widget_set_sensitive(self->priv->menu_reconnect, TRUE);
    gtk_widget_set_sensitive(self->priv->menu_disconnect, FALSE);
    gtk_widget_set_sensitive(self->priv->toolbar_disconnect, FALSE);
    gtk_widget_set_sensitive(self->priv->toolbar_reconnect, TRUE);

    if(gtk_widget_get_visible(self->priv->password_entry))
    {
        gtk_widget_hide(self->priv->password_entry);

        gtk_widget_show(self->priv->textviewscroll);
        gtk_widget_show(self->priv->textview);

        gtk_widget_grab_focus(self->priv->textview);
    }
}

void
mud_window_update_logging_ui(MudWindow *window,
                             MudConnectionView *view,
                             gboolean enabled)
{
    g_return_if_fail(IS_MUD_WINDOW(window));
    g_return_if_fail(IS_MUD_CONNECTION_VIEW(view));

    if(g_direct_equal(window->priv->current_view, view))
    {
        if(enabled)
        {
            gtk_widget_set_sensitive(window->priv->startlog, FALSE);
            gtk_widget_set_sensitive(window->priv->stoplog, TRUE);
        }
        else
        {
            gtk_widget_set_sensitive(window->priv->startlog, TRUE);
            gtk_widget_set_sensitive(window->priv->stoplog, FALSE);
        }
    }
}


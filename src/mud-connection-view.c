/* mud-connection-view.c
 *
 * Copyright 1998-2006 Robin Ericsson <lobbin@localhost.nu>
 * Copyright 2018 Mart Raudsepp <leio@gentoo.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <glib-object.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <vte/vte.h>
#include <string.h>
#include <glib/gprintf.h>

#include "gnome-mud.h"
#include "mud-connection-view.h"
#include "mud-profile.h"
#include "mud-window.h"
#include "mud-tray.h"
#include "mud-log.h"
#include "mud-parse-base.h"
#include "mud-telnet.h"
#include "mud-line-buffer.h"
#include "mud-subwindow.h"
#include "utils.h"

#include "handlers/mud-telnet-handlers.h"

struct _MudConnectionViewPrivate
{
    GtkWidget *scrollbar;
    GtkWidget *popup_menu;
    GtkWidget *progressbar;
    GtkWidget *dl_label;
    GtkWidget *dl_button;

    GString *processed;
    
    gulong signal;
    gulong signal_profile_changed;

    GQueue *history;
    gint current_history_index;

    gchar *current_output;
    GList *subwindows;

    MudLineBuffer *line_buffer;

#ifdef ENABLE_GST
    GQueue *download_queue;
    GCancellable *download_cancel;
    gboolean downloading;
#endif
};

/* Property Identifiers */
enum
{
    PROP_MUD_CONNECTION_VIEW_0,
    PROP_CONNECTION,
    PROP_LOCAL_ECHO,
    PROP_REMOTE_ENCODE,
    PROP_CONNECT_HOOK,
    PROP_CONNECTED,
    PROP_CONNECT_STRING,
    PROP_REMOTE_ENCODING,
    PROP_PORT,
    PROP_MUD_NAME,
    PROP_HOSTNAME,
    PROP_LOG,
    PROP_TRAY,
    PROP_PROFILE,
    PROP_PARSE_BASE,
    PROP_TELNET,
    PROP_WINDOW,
    PROP_LOGGING,
    PROP_TERMINAL,
    PROP_VBOX
};

/* Create the Type */
G_DEFINE_TYPE(MudConnectionView, mud_connection_view, G_TYPE_OBJECT);

/* Class Functions */
static void mud_connection_view_init (MudConnectionView *connection_view);
static void mud_connection_view_class_init (MudConnectionViewClass *klass);
static void mud_connection_view_finalize (GObject *object);

static GObject *mud_connection_view_constructor(GType gtype,
                                                guint n_properties,
                                                GObjectConstructParam *props);

static void mud_connection_view_set_property(GObject *object,
                                             guint prop_id,
                                             const GValue *value,
                                             GParamSpec *pspec);

static void mud_connection_view_get_property(GObject *object,
                                             guint prop_id,
                                             GValue *value,
                                             GParamSpec *pspec);

/* Callbacks */
static gboolean mud_connection_view_button_press_event(GtkWidget *widget, 
                                                       GdkEventButton *event,
                                                       MudConnectionView *view);
static void mud_connection_view_popup(MudConnectionView *view, 
                                      GdkEventButton *event);

static void popup_menu_detach(GtkWidget *widget, GtkMenu *menu);
static void mud_connection_view_close_current_cb(GtkWidget *menu_item,
                                                 MudConnectionView *view);
static void mud_connection_view_profile_changed_cb(MudProfile *profile,
                                                   MudProfileMask *mask,
                                                   MudConnectionView *view);
static void mud_connection_view_line_added_cb(MudLineBuffer *buffer,
                                              MudLineBufferLine *line,
                                              MudConnectionView *view);
static void mud_connection_view_partial_line_cb(MudLineBuffer *buffer,
                                                const GString *line,
                                                MudConnectionView *view);


/* Private Methods */
static void mud_connection_view_set_terminal_colors(MudConnectionView *view);
static void mud_connection_view_set_terminal_scrollback(MudConnectionView *view);
static void mud_connection_view_set_terminal_scrolloutput(MudConnectionView *view);
static void mud_connection_view_set_terminal_font(MudConnectionView *view);
static GtkWidget* append_stock_menuitem(GtkWidget *menu, 
                                        const gchar *text,
                                        GCallback callback,
                                        gpointer data);
static GtkWidget* append_menuitem(GtkWidget *menu,
                                  const gchar *text,
                                  GCallback callback,
                                  gpointer data);
static void choose_profile_callback(GtkWidget *menu_item,
                                    MudConnectionView *view);
static void mud_connection_view_reread_profile(MudConnectionView *view);
static void mud_connection_view_feed_text(MudConnectionView *view,
                                          gchar *message);

static void mud_connection_view_update_geometry (MudConnectionView *window);

#ifdef ENABLE_GST
static void mud_connection_view_download_progress_cb(goffset current_num_bytes,
                                                     goffset total_num_bytes,
                                                     gpointer user_data);
static void mud_connection_view_download_complete_cb(GObject *source_object,
                                                     GAsyncResult *res,
                                                     gpointer user_data);
static void mud_connection_view_download_cancel_cb(GtkWidget *widget,
                                                   MudConnectionView *view);
#endif

/* Class Functions */
static void
mud_connection_view_class_init (MudConnectionViewClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    /* Override base object constructor */
    object_class->constructor = mud_connection_view_constructor;

    /* Override base object's finalize */
    object_class->finalize = mud_connection_view_finalize;

    /* Override base object property methods */
    object_class->set_property = mud_connection_view_set_property;
    object_class->get_property = mud_connection_view_get_property;

    /* Add private data to class */
    g_type_class_add_private(klass, sizeof(MudConnectionViewPrivate));

    /* Create and install properties */
    
    // Required construction properties
    g_object_class_install_property(object_class,
            PROP_CONNECT_STRING,
            g_param_spec_string("connect-string",
                "connect string",
                "string to send to the mud on connect",
                NULL,
                G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property(object_class,
            PROP_PORT,
            g_param_spec_int("port",
                "port",
                "the port of the mud",
                1, 200000,
                23,
                G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property(object_class,
            PROP_MUD_NAME,
            g_param_spec_string("mud-name",
                "mud name",
                "the name of the mud",
                NULL,
                G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property(object_class,
            PROP_HOSTNAME,
            g_param_spec_string("hostname",
                "hostname",
                "the host of the mud",
                NULL,
                G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property(object_class,
            PROP_WINDOW,
            g_param_spec_object("window",
                "window",
                "the parent mud window object",
                MUD_TYPE_WINDOW,
                G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    // Setable properties
    g_object_class_install_property(object_class,
            PROP_REMOTE_ENCODE,
            g_param_spec_boolean("remote-encode",
                "remote encode",
                "do we accept encoding negotiation",
                FALSE,
                G_PARAM_READWRITE));

    g_object_class_install_property(object_class,
            PROP_REMOTE_ENCODING,
            g_param_spec_string("remote-encoding",
                "remote encoding",
                "the negotiated encoding of the terminal",
                NULL,
                G_PARAM_READWRITE));

    g_object_class_install_property(object_class,
            PROP_LOCAL_ECHO,
            g_param_spec_boolean("local-echo",
                "local echo",
                "do we display the text sent to the mud",
                TRUE,
                G_PARAM_READWRITE));

    // Readable Properties
    g_object_class_install_property(object_class,
            PROP_TRAY,
            g_param_spec_object("tray",
                "mud tray",
                "mud status tray icon",
                MUD_TYPE_TRAY,
                G_PARAM_READABLE));

    g_object_class_install_property(object_class,
            PROP_CONNECTION,
            g_param_spec_pointer("connection",
                "connection",
                "the connection to the mud",
                G_PARAM_READABLE));

    g_object_class_install_property(object_class,
            PROP_LOGGING,
            g_param_spec_boolean("logging",
                "logging",
                "are we currently logging",
                FALSE,
                G_PARAM_READWRITE));

    g_object_class_install_property(object_class,
            PROP_CONNECT_HOOK,
            g_param_spec_boolean("connect-hook",
                "connect hook",
                "do we need to send the connection string",
                FALSE,
                G_PARAM_READABLE));
    
    g_object_class_install_property(object_class,
            PROP_CONNECTED,
            g_param_spec_boolean("connected",
                "connected",
                "are we connected to the mud",
                FALSE,
                G_PARAM_READABLE));

    g_object_class_install_property(object_class,
            PROP_LOG,
            g_param_spec_object("log",
                "log",
                "the mud log object",
                MUD_TYPE_LOG,
                G_PARAM_READABLE));

    g_object_class_install_property(object_class,
            PROP_PROFILE,
            g_param_spec_object("profile",
                "profile",
                "the mud profile object",
                MUD_TYPE_PROFILE,
                G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY)); /* TODO: Make it changeable again */

    g_object_class_install_property(object_class,
            PROP_TELNET,
            g_param_spec_object("telnet",
                "telnet",
                "the mud telnet object",
                MUD_TYPE_TELNET,
                G_PARAM_READABLE));

    g_object_class_install_property(object_class,
            PROP_PARSE_BASE,
            g_param_spec_object("parse-base",
                "parse base",
                "the parse base object",
                MUD_TYPE_PARSE_BASE,
                G_PARAM_READABLE));

    g_object_class_install_property(object_class,
            PROP_TERMINAL,
            g_param_spec_object("terminal",
                "terminal",
                "the terminal widget",
                VTE_TYPE_TERMINAL,
                G_PARAM_READABLE));

    g_object_class_install_property(object_class,
            PROP_VBOX,
            g_param_spec_object("ui-vbox",
                "ui vbox",
                "main ui vbox",
                GTK_TYPE_VBOX,
                G_PARAM_READABLE));
}

static void
mud_connection_view_init (MudConnectionView *self)
{
    /* Get our private data */
    self->priv = MUD_CONNECTION_VIEW_GET_PRIVATE(self);

    /* Set some defaults */
    self->priv->history = g_queue_new();
    self->priv->current_history_index = 0;

    self->priv->processed = NULL;

    self->local_echo = TRUE;
    self->remote_encode = FALSE;   
    self->connect_hook = FALSE;

    self->connect_string = NULL;
    self->remote_encoding = NULL;

    self->port = 23;
    self->mud_name = NULL;
    self->hostname = NULL;

    self->log = NULL;
    self->tray = NULL;
    self->profile = NULL;
    self->parse = NULL;
    self->window = NULL;
    self->telnet = NULL;

    self->terminal = NULL;
    self->ui_vbox = NULL;

    self->priv->subwindows = NULL;
    self->priv->current_output = g_strdup("main");

    self->priv->line_buffer = g_object_new(MUD_TYPE_LINE_BUFFER,
                                           NULL);
}

static void
mud_connection_connected_cb(MudConnection *conn, MudConnectionView *self)
{
    self->telnet = g_object_new(MUD_TYPE_TELNET,
                                "parent-view", self,
                                "connection", conn,
                                NULL);

    mud_tray_update_icon(self->tray, online);
    mud_connection_view_add_text(self, _("*** Connected.\n"), System);
}

static void
mud_connection_disconnected_cb(MudConnection *conn, MudConnectionView *self)
{
    g_clear_object(&self->telnet);

    /* We may have been disconnected with local echo disabled, it must be
     * enabled to display the connection closed message.
     */
    self->local_echo = TRUE;

    mud_window_disconnected(self->window);
    mud_connection_view_add_text(self, _("\n*** Connection closed.\n"), Error);
}

static void
mud_connection_data_ready_cb(MudConnection *conn, MudConnectionView *self)
{
    const GByteArray *input;
    gint tlen;

    input = mud_connection_get_input(conn);

    self->priv->processed = mud_telnet_process(self->telnet,
                                               input->data,
                                               input->len,
                                               &tlen);

    if(self->priv->processed)
    {
        mud_line_buffer_add_data(self->priv->line_buffer,
                                 self->priv->processed->str,
                                 self->priv->processed->len);
        g_string_free(self->priv->processed, TRUE);
        self->priv->processed = NULL;
    }
}

/* TODO: Support loading the system-wide proxy. */
static gchar *
mud_connection_view_create_proxy_uri(const MudConnectionView *self)
{
    gchar *uri = NULL;
    gchar *host, *str_version;
    guint version;

    if (!g_settings_get_boolean(self->profile->settings, "use-proxy"))
        return NULL;

    host = g_settings_get_string(self->profile->settings, "proxy-hostname");
    str_version = g_settings_get_string(self->profile->settings, "proxy-socks-version");
    if(strcmp(str_version, "4") == 0)
        version = 4;
    else
        version = 5;

    uri = g_strdup_printf("socks%d://%s", version, host);

    g_free(host);
    g_free(str_version);

    return uri;
}

static GObject *
mud_connection_view_constructor (GType gtype,
                                 guint n_properties,
                                 GObjectConstructParam *properties)
{
    GtkWidget *box;
#ifdef ENABLE_GST
    GtkWidget *dl_vbox;
    GtkWidget *dl_hbox;
#endif
    GtkWidget *term_box;
    GtkWidget *main_window;
    MudTray *tray;
    gchar *buf;
    gchar *proxy_uri;
    MudProfile *profile;
    MudConnectionView *self;
    GObject *obj;

    MudConnectionViewClass *klass;
    GObjectClass *parent_class;

    /* Chain up to parent constructor */
    klass = MUD_CONNECTION_VIEW_CLASS( g_type_class_peek(MUD_TYPE_CONNECTION_VIEW) );
    parent_class = G_OBJECT_CLASS( g_type_class_peek_parent(klass) );
    obj = parent_class->constructor(gtype, n_properties, properties);

    /* Construct the View */
    self = MUD_CONNECTION_VIEW(obj);

    /* Check for construction properties */
    if(!self->mud_name)
    {
        g_printf("ERROR: Tried to instantiate MudConnectionView without passing mud-name.\n");
        g_error("Tried to instantiate MudConnectionView without passing mud-name.");
    }
    
    if(!self->hostname)
    {
        g_printf("ERROR: Tried to instantiate MudConnectionView without passing hostname.\n");
        g_error("Tried to instantiate MudConnectionView without passing hostname.");
    }

    if(!self->window)
    {
        g_printf("ERROR: Tried to instantiate MudConnectionView without passing parent MudWindow.\n");
        g_error("Tried to instantiate MudConnectionView without passing parent MudWindow.");
    }

    /* Create main VBox, VTE, and scrollbar */
    box = gtk_vbox_new(FALSE, 0);
    self->ui_vbox = GTK_VBOX(box);
    self->terminal = VTE_TERMINAL(vte_terminal_new()); /* TODO: Set up autowrap, so things rewrap on resize (this will be the default once we upgrade VTE to some gtk3 version); make sure none of our own tracking of things gets messed up from that, though */
    self->priv->scrollbar = gtk_vscrollbar_new(NULL);
    term_box = gtk_hbox_new(FALSE, 0);

#ifdef ENABLE_GST
    /* Setup Download UI */
    dl_vbox = gtk_vbox_new(FALSE, 0);
    dl_hbox = gtk_hbox_new(FALSE, 0);

    self->priv->dl_label = gtk_label_new("Downloading…");
    self->priv->progressbar = gtk_progress_bar_new();
    gtk_progress_bar_set_pulse_step (GTK_PROGRESS_BAR(self->priv->progressbar), 0.1);
    self->priv->dl_button = gtk_button_new_from_stock("gtk-cancel");

    /* Pack the Download UI */
    gtk_box_pack_start(GTK_BOX(dl_vbox), self->priv->dl_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(dl_hbox), self->priv->progressbar, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(dl_hbox), self->priv->dl_button, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(dl_vbox), dl_hbox, TRUE, TRUE, 0);

    /* Pack into Main UI */
    gtk_box_pack_start(GTK_BOX(box), dl_vbox, FALSE, FALSE, 0);

    /* Set defaults and create download queue */
    self->priv->downloading = FALSE;
    self->priv->download_queue = g_queue_new();

    /* Connect Download Cancel Signal */
    g_signal_connect(self->priv->dl_button,
                     "clicked",
                     G_CALLBACK(mud_connection_view_download_cancel_cb),
                     self);
#endif

    /* Pack the Terminal UI */
    gtk_box_pack_start(GTK_BOX(term_box),
                       GTK_WIDGET(self->terminal),
                       TRUE,
                       TRUE,
                       0);

    gtk_box_pack_end(GTK_BOX(term_box),
                     self->priv->scrollbar, 
                     FALSE,
                     FALSE,
                     0);

    /* Pack into Main UI */
    gtk_box_pack_end(GTK_BOX(box), term_box, TRUE, TRUE, 0);

    /* Connect signals and set data */
    g_signal_connect(G_OBJECT(self->terminal),
                     "button_press_event",
                     G_CALLBACK(mud_connection_view_button_press_event),
                     self);

    g_object_set_data(G_OBJECT(self->terminal),
                      "connection-view",
                      self);
    g_object_set_data(G_OBJECT(box),
                      "connection-view",
                      self);

    /* Setup scrollbar */
    gtk_range_set_adjustment(
            GTK_RANGE(self->priv->scrollbar),
            self->terminal->adjustment);

    /* Setup VTE */
    vte_terminal_set_encoding(self->terminal, "ISO-8859-1");
    vte_terminal_set_emulation(self->terminal, "xterm");
    vte_terminal_set_cursor_shape(self->terminal,
                                  VTE_CURSOR_SHAPE_UNDERLINE);

    vte_terminal_set_cursor_blink_mode(self->terminal,
                                       VTE_CURSOR_BLINK_OFF);

    g_object_get(self->window,
                 "window", &main_window,
                 "tray", &tray,
                 NULL);

    profile = self->profile; /* FIXME: Gross workaround to get around set_profile check and early bail-out after construct_only restructuring; hopefully this can all be cleaned up together with move away from constructor to constructed override */
    self->profile = NULL;
    mud_connection_view_set_profile(self, profile);

    self->tray = tray;

    self->parse = g_object_new(MUD_TYPE_PARSE_BASE,
                               "parent-view", self,
                               NULL);

    self->log = g_object_new(MUD_TYPE_LOG,
                             "mud-name", self->mud_name,
                             "window", self->window,
                             "parent", self,
                              NULL);

    buf = g_strdup_printf(_("*** Making connection to %s, port %d.\n"),
            self->hostname, self->port);
    mud_connection_view_add_text(self, buf, System);
    g_free(buf);
    buf = NULL;

    self->conn = g_object_new(MUD_TYPE_CONNECTION, NULL);

    g_signal_connect(self->conn, "connected",
                     G_CALLBACK(mud_connection_connected_cb), self);

    g_signal_connect(self->conn, "disconnected",
                     G_CALLBACK(mud_connection_disconnected_cb), self);

    g_signal_connect(self->conn, "data-ready",
                     G_CALLBACK(mud_connection_data_ready_cb), self);

    proxy_uri = mud_connection_view_create_proxy_uri(self);
    mud_connection_connect(self->conn, self->hostname, self->port, proxy_uri);
    g_free(proxy_uri);

    /* Show everything */
    gtk_widget_show_all(box);

    mud_connection_view_update_geometry (self);

#ifdef ENABLE_GST
    /* Hide UI until download starts */
    gtk_widget_hide(self->priv->progressbar);
    gtk_widget_hide(self->priv->dl_label);
    gtk_widget_hide(self->priv->dl_button);
#endif

    g_signal_connect(self->priv->line_buffer,
                     "line-added",
                     G_CALLBACK(mud_connection_view_line_added_cb),
                     self);

    g_signal_connect(self->priv->line_buffer,
                     "partial-line-received",
                     G_CALLBACK(mud_connection_view_partial_line_cb),
                     self);

    return obj;
}

static void
mud_connection_view_finalize (GObject *object)
{
    MudConnectionView *connection_view;
    GObjectClass *parent_class;
    gchar *history_item;
    GList *entry;

#ifdef ENABLE_GST                               
    MudMSPDownloadItem *item;
#endif

    connection_view = MUD_CONNECTION_VIEW(object);

    if(connection_view->priv->history && !g_queue_is_empty(connection_view->priv->history))
        while((history_item = (gchar *)g_queue_pop_head(connection_view->priv->history)) != NULL)
            g_free(history_item);

    if(connection_view->priv->history)
        g_queue_free(connection_view->priv->history);

#ifdef ENABLE_GST                               
    if(connection_view->priv->download_queue
            && !g_queue_is_empty(connection_view->priv->download_queue))
        while((item = (MudMSPDownloadItem *)
                    g_queue_pop_head(connection_view->priv->download_queue)) != NULL)
            mud_telnet_msp_download_item_free(item);

    if(connection_view->priv->download_queue)
        g_queue_free(connection_view->priv->download_queue);
#endif

    if(connection_view->conn)
        g_object_unref(connection_view->conn);

    if(connection_view->hostname)
        g_free(connection_view->hostname);

    if(connection_view->connect_string)
        g_free(connection_view->connect_string);
    
    if(connection_view->mud_name)
        g_free(connection_view->mud_name);

    if(connection_view->priv->processed)
        g_string_free(connection_view->priv->processed, TRUE);
    
    g_object_unref(connection_view->log);

    g_object_unref(connection_view->parse);

    /* TODO: We don't hold a ref right now - we should, or keep a weakref?
     * if(connection_view->profile)
     *  g_object_unref(connection_view->profile);
     */

    g_object_unref(connection_view->priv->line_buffer);

    entry = g_list_first(connection_view->priv->subwindows);

    while(entry)
    {
        MudSubwindow *sub = MUD_SUBWINDOW(entry->data);

        g_object_unref(sub);

        entry = g_list_next(entry);
    }

    g_list_free(connection_view->priv->subwindows);

    parent_class = g_type_class_peek_parent(G_OBJECT_GET_CLASS(object));
    parent_class->finalize(object);
}


static void
mud_connection_view_set_property(GObject *object,
                                 guint prop_id,
                                 const GValue *value,
                                 GParamSpec *pspec)
{
    MudConnectionView *self;
    gboolean new_boolean;
    gint new_int;
    gchar *new_string;

    self = MUD_CONNECTION_VIEW(object);

    switch(prop_id)
    {
        case PROP_REMOTE_ENCODE:
            new_boolean = g_value_get_boolean(value);

            if(new_boolean != self->remote_encode)
                self->remote_encode = new_boolean;
            break;

        case PROP_CONNECT_STRING:
            new_string = g_value_dup_string(value);

            if(!self->connect_string)
                self->connect_string = 
                    (new_string) ? g_strdup(new_string) : NULL;
            else if( strcmp(self->connect_string, new_string) != 0)
            {
                if(self->connect_string)
                    g_free(self->connect_string);
                self->connect_string = 
                    (new_string) ? g_strdup(new_string) : NULL;
            }

            if(self->connect_string)
                self->connect_hook = TRUE;

            g_free(new_string);
            break;

        case PROP_REMOTE_ENCODING:
            new_string = g_value_dup_string(value);

            if(!self->remote_encoding)
                self->remote_encoding = 
                    (new_string) ? g_strdup(new_string) : NULL;
            else if( strcmp(self->remote_encoding, new_string) != 0)
            {
                if(self->remote_encoding)
                    g_free(self->remote_encoding);
                self->remote_encoding = 
                    (new_string) ? g_strdup(new_string) : NULL;
            }

            g_free(new_string);
            break;

        case PROP_PORT:
            new_int = g_value_get_int(value);

            if(new_int != self->port)
                self->port = new_int;
            break;

        case PROP_LOCAL_ECHO:
            new_boolean = g_value_get_boolean(value);

            if(new_boolean != self->local_echo)
            {
                self->local_echo = new_boolean;
                mud_window_toggle_input_mode(self->window, self);
            }
            break;

        case PROP_MUD_NAME:
            new_string = g_value_dup_string(value);

            if(!self->mud_name)
                self->mud_name = 
                    (new_string) ? g_strdup(new_string) : NULL;
            else if( strcmp(self->mud_name, new_string) != 0)
            {
                if(self->mud_name)
                    g_free(self->mud_name);
                self->mud_name = 
                    (new_string) ? g_strdup(new_string) : NULL;
            }

            g_free(new_string);
            break;

        case PROP_HOSTNAME:
            new_string = g_value_dup_string(value);

            if(!self->hostname)
                self->hostname = 
                    (new_string) ? g_strdup(new_string) : NULL;
            else if( strcmp(self->hostname, new_string) != 0)
            {
                if(self->hostname)
                    g_free(self->hostname);
                self->hostname = 
                    (new_string) ? g_strdup(new_string) : NULL;
            }

            g_free(new_string);
            break;

        case PROP_PROFILE:
            self->profile = g_value_get_object (value);
            break;

        case PROP_WINDOW:
            self->window = MUD_WINDOW(g_value_get_object(value));
            break;

        case PROP_TRAY:
            self->tray = MUD_TRAY(g_value_get_object(value));
            break;

        case PROP_LOGGING:
            new_boolean = g_value_get_boolean(value);

            if(new_boolean != self->logging)
                self->logging = new_boolean;
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void
mud_connection_view_get_property(GObject *object,
                                 guint prop_id,
                                 GValue *value,
                                 GParamSpec *pspec)
{
    MudConnectionView *self;

    self = MUD_CONNECTION_VIEW(object);

    switch(prop_id)
    {
        case PROP_CONNECTION:
            g_value_set_pointer(value, self->conn);
            break;

        case PROP_LOCAL_ECHO:
            g_value_set_boolean(value, self->local_echo);
            break;

        case PROP_REMOTE_ENCODE:
            g_value_set_boolean(value, self->remote_encode);
            break;

        case PROP_CONNECT_HOOK:
            g_value_set_boolean(value, self->connect_hook);
            break;

        case PROP_CONNECTED:
            g_value_set_boolean(value, mud_connection_is_connected(self->conn));
            break;

        case PROP_CONNECT_STRING:
            g_value_set_string(value, self->connect_string);
            break;

        case PROP_REMOTE_ENCODING:
            g_value_set_string(value, self->remote_encoding);
            break;

        case PROP_PORT:
            g_value_set_int(value, self->port);
            break;

        case PROP_MUD_NAME:
            g_value_set_string(value, self->mud_name);
            break;

        case PROP_HOSTNAME:
            g_value_set_string(value, self->hostname);
            break;

        case PROP_LOG:
            g_value_take_object(value, self->log);
            break;

        case PROP_TRAY:
            g_value_take_object(value, self->tray);
            break;

        case PROP_PROFILE:
            g_value_take_object(value, self->profile);
            break;

        case PROP_PARSE_BASE:
            g_value_take_object(value, self->parse);
            break;

        case PROP_WINDOW:
            g_value_take_object(value, self->window);
            break;

        case PROP_TELNET:
            g_value_take_object(value, self->telnet);
            break;

        case PROP_LOGGING:
            g_value_set_boolean(value, self->logging);
            break;

        case PROP_TERMINAL:
            g_value_take_object(value, self->terminal);
            break;

        case PROP_VBOX:
            g_value_take_object(value, self->ui_vbox);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

/* Callbacks */
static void
choose_profile_callback(GtkWidget *menu_item, MudConnectionView *view)
{
    MudProfile *profile;

    if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(menu_item)))
        return;

    profile = g_object_get_data(G_OBJECT(menu_item), "profile");

    g_assert(profile);

    mud_connection_view_set_profile(view, profile);
    mud_connection_view_reread_profile(view);
    mud_window_populate_profiles_menu(view->window);
}

static void
mud_connection_view_close_current_cb(GtkWidget *menu_item, MudConnectionView *view)
{
    mud_window_close_current_window(view->window);
}

static void
mud_connection_view_profile_changed_cb(MudProfile *profile, MudProfileMask *mask, MudConnectionView *view)
{
    GList *entry;

    if (mask->ScrollOnOutput)
        mud_connection_view_set_terminal_scrolloutput(view);
    if (mask->Scrollback)
        mud_connection_view_set_terminal_scrollback(view);
    if (mask->FontName)
        mud_connection_view_set_terminal_font(view);
    if (mask->Foreground || mask->Background || mask->Colors)
        mud_connection_view_set_terminal_colors(view);

    entry = g_list_first(view->priv->subwindows);

    while(entry)
    {
        MudSubwindow *sub = MUD_SUBWINDOW(entry->data);

        mud_subwindow_reread_profile(sub);

        entry = g_list_next(entry);
    }
}

static gboolean
mud_connection_view_button_press_event(GtkWidget *widget, GdkEventButton *event, MudConnectionView *view)
{
    if ((event->button == 3) &&
            !(event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK)))
    {
        mud_connection_view_popup(view, event);
        return TRUE;
    }

    return FALSE;
}

static void
mud_connection_view_popup(MudConnectionView *view, GdkEventButton *event)
{
    GtkWidget *im_menu;
    GtkWidget *menu_item;
    GtkWidget *profile_menu;
    GSequence *profiles;
    GSequenceIter *iter;
    GSList *group;

    if (view->priv->popup_menu)
        gtk_widget_destroy(view->priv->popup_menu);

    g_assert(view->priv->popup_menu == NULL);

    view->priv->popup_menu = gtk_menu_new();
    gtk_menu_attach_to_widget(GTK_MENU(view->priv->popup_menu),
            GTK_WIDGET(view->terminal),
            popup_menu_detach);

    append_menuitem(view->priv->popup_menu,
            _("Close"),
            G_CALLBACK(mud_connection_view_close_current_cb),
            view);

    menu_item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(view->priv->popup_menu), menu_item);

    append_stock_menuitem(view->priv->popup_menu,
            GTK_STOCK_COPY,
            NULL,
            view);
    append_stock_menuitem(view->priv->popup_menu,
            GTK_STOCK_PASTE,
            NULL,
            view);

    menu_item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(view->priv->popup_menu), menu_item);

    profile_menu = gtk_menu_new();
    menu_item = gtk_menu_item_new_with_mnemonic(_("Change P_rofile"));
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item), profile_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(view->priv->popup_menu), menu_item);

    group = NULL;
    profiles = mud_profile_manager_get_profiles(view->window->profile_manager);

    for (iter = g_sequence_get_begin_iter (profiles);
         !g_sequence_iter_is_end (iter);
         iter = g_sequence_iter_next (iter))
    {
        MudProfile *prof = g_sequence_get (iter);

        /* Profiles can go away while the menu is up. */
        g_object_ref(G_OBJECT(prof)); /* TODO: Review reference handling (weakref?) */

        menu_item = gtk_radio_menu_item_new_with_label(group,
                prof->visible_name);
        group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menu_item));
        gtk_menu_shell_append(GTK_MENU_SHELL(profile_menu), menu_item);

        if (prof == view->profile)
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item), TRUE);

        g_signal_connect(G_OBJECT(menu_item),
                "toggled",
                G_CALLBACK(choose_profile_callback),
                view);
        g_object_set_data_full(G_OBJECT(menu_item),
                "profile",
                prof,
                (GDestroyNotify) g_object_unref);
    }

    menu_item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(view->priv->popup_menu), menu_item);

    im_menu = gtk_menu_new();
    menu_item = gtk_menu_item_new_with_mnemonic(_("_Input Methods"));
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item), im_menu);
    vte_terminal_im_append_menuitems(view->terminal,
            GTK_MENU_SHELL(im_menu));
    gtk_menu_shell_append(GTK_MENU_SHELL(view->priv->popup_menu), menu_item);

    gtk_widget_show_all(view->priv->popup_menu);
    gtk_menu_popup(GTK_MENU(view->priv->popup_menu),
            NULL, NULL,
            NULL, NULL,
            event ? event->button : 0,
            event ? event->time : gtk_get_current_event_time());
}

static void
mud_connection_view_line_added_cb(MudLineBuffer *buffer,
                                  MudLineBufferLine *line,
                                  MudConnectionView *view)
{
#ifdef ENABLE_GST
    MudTelnetMsp *msp_handler;
    gboolean msp_parser_enabled;

    msp_handler =
        MUD_TELNET_MSP(mud_telnet_get_handler(view->telnet,
                    TELOPT_MSP));

    g_object_get(msp_handler,
            "enabled", &msp_parser_enabled,
            NULL);

    if(msp_parser_enabled)
    {
        mud_telnet_msp_parse(msp_handler,
                             line);

        mud_telnet_msp_parser_clear(msp_handler);

        if(line->gag)
            return;
    }
#endif

    // TODO: Trigger & script code.

    if(!line->gag)
    {
        if(g_str_equal(view->priv->current_output, "main"))
        {
            vte_terminal_feed(view->terminal,
                              line->line->str,
                              line->line->len);

            mud_window_toggle_tab_icon(view->window, view);
        }
        else
        {
            MudSubwindow *sub =
                mud_connection_view_get_subwindow(view,
                        view->priv->current_output);

            if(sub)
                mud_subwindow_feed(sub, line->line->str, line->line->len);
            else
            {
                vte_terminal_feed(view->terminal,
                                  line->line->str,
                                  line->line->len);

                mud_window_toggle_tab_icon(view->window, view);
            }
        }

        if (view->connect_hook)
        {
            mud_connection_view_send (view, view->connect_string);
            view->connect_hook = FALSE;
        }
    }

    if(view->logging)
        mud_log_write_hook(view->log, line->line->str, line->line->len);
}

static void
mud_connection_view_partial_line_cb(MudLineBuffer *buffer,
                                    const GString *line,
                                    MudConnectionView *view)
{
    //TODO:  Pass through trigger and script code.
    
    if(!mud_line_buffer_partial_clear(buffer))
    {
        mud_line_buffer_clear_partial_line(buffer);

        if(g_str_equal(view->priv->current_output, "main"))
        {
            vte_terminal_feed(view->terminal,
                    line->str,
                    line->len);

            mud_window_toggle_tab_icon(view->window, view);
        }
        else
        {
            MudSubwindow *sub =
                mud_connection_view_get_subwindow(view,
                        view->priv->current_output);

            if(sub)
                mud_subwindow_feed(sub, line->str, line->len);
            else
            {
                vte_terminal_feed(view->terminal,
                        line->str,
                        line->len);

                mud_window_toggle_tab_icon(view->window, view);
            }

        }

    }

    if(view->logging)
        mud_log_write_hook(view->log, line->str, line->len);
}

static void
popup_menu_detach(GtkWidget *widget, GtkMenu *menu)
{
    MudConnectionView *view;

    view = g_object_get_data(G_OBJECT(widget), "connection-view");
    view->priv->popup_menu = NULL;
}

/* Private Methods */
static void
mud_connection_view_update_geometry (MudConnectionView *window)
{
    /* TODO: switch to vte_terminal_set_geometry_hints_for_window */
    GtkWidget *widget = GTK_WIDGET(window->terminal);
    GtkWidget *mainwindow;
    GdkGeometry hints;
    gint char_width;
    gint char_height;
    gint xpad, ypad;

    char_width = VTE_TERMINAL(widget)->char_width;
    char_height = VTE_TERMINAL(widget)->char_height;

    vte_terminal_get_padding (VTE_TERMINAL (window->terminal), &xpad, &ypad);

    hints.base_width = xpad;
    hints.base_height = ypad;

#define MIN_WIDTH_CHARS 4
#define MIN_HEIGHT_CHARS 2

    hints.width_inc = char_width;
    hints.height_inc = char_height;

    /* min size is min size of just the geometry widget, remember. */
    hints.min_width = hints.base_width + hints.width_inc * MIN_WIDTH_CHARS;
    hints.min_height = hints.base_height + hints.height_inc * MIN_HEIGHT_CHARS;

    g_object_get(window->window, "window", &mainwindow, NULL);
    gtk_window_set_geometry_hints (GTK_WINDOW (mainwindow),
            widget,
            &hints,
            GDK_HINT_RESIZE_INC |
            GDK_HINT_MIN_SIZE |
            GDK_HINT_BASE_SIZE);
}

static GtkWidget*
append_stock_menuitem(GtkWidget *menu, const gchar *text, GCallback callback, gpointer data)
{
    GtkWidget *menu_item;

    menu_item = gtk_image_menu_item_new_from_stock(text, NULL);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);

    if (callback)
        g_signal_connect(G_OBJECT(menu_item),
                         "activate",
                         callback, data);

    return menu_item;
}

static GtkWidget*
append_menuitem(GtkWidget *menu, const gchar *text, GCallback callback, gpointer data)
{
    GtkWidget *menu_item;

    menu_item = gtk_menu_item_new_with_mnemonic(text);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);

    if (callback)
        g_signal_connect(G_OBJECT(menu_item), "activate", callback, data);

    return menu_item;
}


static void
mud_connection_view_feed_text(MudConnectionView *view, gchar *message)
{
    gint rlen = strlen(message);
    gchar buf[rlen*2];

    g_return_if_fail(IS_MUD_CONNECTION_VIEW(view));

    g_stpcpy(buf, message);
    utils_str_replace(buf, "\r", "");
    utils_str_replace(buf, "\n", "\n\r");

    vte_terminal_feed(view->terminal, buf, strlen(buf));
}

static void
mud_connection_view_reread_profile(MudConnectionView *view)
{
    g_return_if_fail(IS_MUD_CONNECTION_VIEW(view));

    mud_connection_view_set_terminal_colors(view);
    mud_connection_view_set_terminal_scrollback(view);
    mud_connection_view_set_terminal_scrolloutput(view);
    mud_connection_view_set_terminal_font(view);
}

static void
mud_connection_view_set_terminal_colors(MudConnectionView *view)
{
    g_return_if_fail(IS_MUD_CONNECTION_VIEW(view));

    vte_terminal_set_colors(view->terminal,
            &view->profile->preferences->Foreground,
            &view->profile->preferences->Background,
            view->profile->preferences->Colors, C_MAX);

    vte_terminal_set_color_cursor(view->terminal,
                                  &view->profile->preferences->Background);
}

static void
mud_connection_view_set_terminal_scrollback(MudConnectionView *view)
{
    g_return_if_fail(IS_MUD_CONNECTION_VIEW(view));

    vte_terminal_set_scrollback_lines(view->terminal,
            view->profile->preferences->Scrollback);
}

static void
mud_connection_view_set_terminal_scrolloutput(MudConnectionView *view)
{
    g_return_if_fail(IS_MUD_CONNECTION_VIEW(view));

    if(view->terminal)
        vte_terminal_set_scroll_on_output(view->terminal,
                view->profile->preferences->ScrollOnOutput);
}

static void
mud_connection_view_set_terminal_font(MudConnectionView *view)
{
    PangoFontDescription *desc = NULL;
    char *name;

    g_return_if_fail(IS_MUD_CONNECTION_VIEW(view));

    name = view->profile->preferences->FontName;

    if(name)
        desc = pango_font_description_from_string(name);

    if(!desc)
        desc = pango_font_description_from_string("Monospace 10");

    vte_terminal_set_font(view->terminal, desc);
}

/* Public Methods */
MudSubwindow *
mud_connection_view_create_subwindow(MudConnectionView *view,
                                     const gchar *title,
                                     const gchar *identifier,
                                     guint width,
                                     guint height)
{
    MudSubwindow *sub;

    if(!IS_MUD_CONNECTION_VIEW(view))
        return NULL;

    if(mud_connection_view_has_subwindow(view, identifier))
        return mud_connection_view_get_subwindow(view, identifier);

    sub = g_object_new(MUD_TYPE_SUBWINDOW,
                       "title", title,
                       "identifier", identifier,
                       "width", width,
                       "height", height,
                       "parent-view", view,
                       NULL);

    view->priv->subwindows = g_list_append(view->priv->subwindows, sub);

    return sub;
}

gboolean
mud_connection_view_has_subwindow(MudConnectionView *view,
                                  const gchar *identifier)
{
    GList *entry;
    gchar *ident;

    if(!IS_MUD_CONNECTION_VIEW(view))
        return FALSE;

    entry = g_list_first(view->priv->subwindows);

    while(entry)
    {
        MudSubwindow *sub = MUD_SUBWINDOW(entry->data);

        g_object_get(sub, "identifier", &ident, NULL);

        if(g_str_equal(identifier, ident))
        {
            g_free(ident);

            return TRUE;
        }

        g_free(ident);

        entry = g_list_next(entry);
    }

    return FALSE;
}

void
mud_connection_view_set_output(MudConnectionView *view,
                               const gchar *identifier)
{
    g_return_if_fail(IS_MUD_CONNECTION_VIEW(view));

    if(mud_connection_view_has_subwindow(view, identifier) ||
       g_str_equal(identifier, "main"))
    {
        g_free(view->priv->current_output);
        view->priv->current_output = g_strdup(identifier);
    }
}

void
mud_connection_view_enable_subwindow_input(MudConnectionView *view,
                                           const gchar *identifier,
                                           gboolean enable)
{
    g_return_if_fail(IS_MUD_CONNECTION_VIEW(view));

    if(mud_connection_view_has_subwindow(view, identifier))
    {
        MudSubwindow *sub =
            mud_connection_view_get_subwindow(view, identifier);

        mud_subwindow_enable_input(sub, enable);
    }
}

void
mud_connection_view_enable_subwindow_scroll(MudConnectionView *view,
                                            const gchar *identifier,
                                            gboolean enable)
{
    g_return_if_fail(IS_MUD_CONNECTION_VIEW(view));

    if(mud_connection_view_has_subwindow(view, identifier))
    {
        MudSubwindow *sub =
            mud_connection_view_get_subwindow(view, identifier);

        mud_subwindow_enable_scroll(sub, enable);
    }
}


void
mud_connection_view_show_subwindow(MudConnectionView *view,
                                   const gchar *identifier)
{
    g_return_if_fail(IS_MUD_CONNECTION_VIEW(view));

    if(mud_connection_view_has_subwindow(view, identifier))
    {
        MudSubwindow *sub =
            mud_connection_view_get_subwindow(view, identifier);

        mud_subwindow_show(sub);
    }
}

MudSubwindow *
mud_connection_view_get_subwindow(MudConnectionView *view,
                                  const gchar *identifier)
{
    GList *entry;
    gchar *ident;

    if(!IS_MUD_CONNECTION_VIEW(view))
        return NULL;

    if(!mud_connection_view_has_subwindow(view, identifier))
        return NULL;

    entry = view->priv->subwindows;

    while(entry)
    {
        MudSubwindow *sub = MUD_SUBWINDOW(entry->data);

        g_object_get(sub, "identifier", &ident, NULL);

        if(g_str_equal(ident, identifier))
        {
            g_free(ident);

            return sub;
        }

        g_free(ident);

        entry = g_list_next(entry);
    }

    return NULL;
}

void
mud_connection_view_remove_subwindow(MudConnectionView *view,
                                     const gchar *identifier)
{
    MudSubwindow *sub;

    g_return_if_fail(IS_MUD_CONNECTION_VIEW(view));

    if(!mud_connection_view_has_subwindow(view, identifier))
        return;

    sub = mud_connection_view_get_subwindow(view, identifier);

    if(g_str_equal(view->priv->current_output, identifier))
    {
        g_free(view->priv->current_output);
        view->priv->current_output = g_strdup("main");
    }

    view->priv->subwindows = g_list_remove(view->priv->subwindows, sub);

    g_object_unref(sub);

}

void
mud_connection_view_hide_subwindows(MudConnectionView *view)
{
    GList *entry;
    gboolean visible;

    g_return_if_fail(IS_MUD_CONNECTION_VIEW(view));

    entry = g_list_first(view->priv->subwindows);

    while(entry)
    {
        MudSubwindow *sub = MUD_SUBWINDOW(entry->data);

        g_object_get(sub, "visible", &visible, NULL);

        if(visible)
        {
            g_object_set(sub, "view-hidden", TRUE, NULL);
            mud_subwindow_hide(sub);
        }

        entry = g_list_next(entry);
    }
}

void
mud_connection_view_show_subwindows(MudConnectionView *view)
{
    GList *entry;
    gboolean view_hidden;

    g_return_if_fail(IS_MUD_CONNECTION_VIEW(view));

    entry = g_list_first(view->priv->subwindows);

    while(entry)
    {
        MudSubwindow *sub = MUD_SUBWINDOW(entry->data);

        g_object_get(sub, "view-hidden", &view_hidden, NULL);

        if(view_hidden)
        {
            g_object_set(sub, "view-hidden", FALSE, NULL);
            mud_subwindow_show(sub);
        }

        entry = g_list_next(entry);
    }
}

void
mud_connection_view_add_text(MudConnectionView *view, gchar *message, enum MudConnectionColorType type)
{
    gchar *encoding, *text;
    const gchar *local_codeset;
    gboolean remote;
    gsize bytes_read, bytes_written;
    GError *error = NULL;

    g_return_if_fail(IS_MUD_CONNECTION_VIEW(view));

    remote = g_settings_get_boolean(view->profile->settings, "remote-encoding");

    if(view->remote_encode && remote)
        encoding = g_strdup(view->remote_encoding);
    else
        encoding = g_settings_get_string(view->profile->settings, "encoding");

    g_get_charset(&local_codeset);

    text = g_convert(message, -1,
            encoding,
            local_codeset, 
            &bytes_read, &bytes_written, &error);

    if(error)
    {
        text = NULL;
    }

    vte_terminal_set_encoding(view->terminal, encoding);

    g_free(encoding);

    switch (type)
    {
        case Sent:
            mud_connection_view_feed_text(view, "\e[1;33m");
            break;

        case Error:
            mud_connection_view_feed_text(view, "\e[1;31m");
            break;

        case System:
            mud_connection_view_feed_text(view, "\e[1;32m");
            break;

        case Normal:
        default:
            break;
    }

    if(view->local_echo)
        mud_connection_view_feed_text(view, (!error) ? text : message);

    mud_connection_view_feed_text(view, "\e[0m");

    if(text != NULL)
        g_free(text);
}

void
mud_connection_view_disconnect(MudConnectionView *view)
{
#ifdef ENABLE_GST
    MudMSPDownloadItem *item;
#endif

    g_return_if_fail(IS_MUD_CONNECTION_VIEW(view));

#ifdef ENABLE_GST
    if(view->priv->download_queue)
        while((item = (MudMSPDownloadItem *)g_queue_pop_head(view->priv->download_queue)) != NULL)
            mud_telnet_msp_download_item_free(item);

    if(view->priv->download_queue)
        g_queue_free(view->priv->download_queue);

    view->priv->download_queue = NULL;
#endif

    mud_connection_view_stop_logging(view);

    view->priv->processed = NULL;

    mud_connection_disconnect(view->conn);
}

void
mud_connection_view_reconnect(MudConnectionView *view)
{
    gchar *buf, *proxy_uri;

    g_return_if_fail(IS_MUD_CONNECTION_VIEW(view));

#ifdef ENABLE_GST
    view->priv->download_queue = g_queue_new();
#endif

    view->local_echo = TRUE;

    buf = g_strdup_printf(_("*** Making connection to %s, port %d.\n"),
            view->hostname, view->port);
    mud_connection_view_add_text(view, buf, System);
    g_free(buf);

    proxy_uri = mud_connection_view_create_proxy_uri(view);
    mud_connection_connect(view->conn, view->hostname, view->port, proxy_uri);
    g_free(proxy_uri);
}

void
mud_connection_view_send(MudConnectionView *view, const gchar *data)
{
    MudTelnetZmp *zmp_handler;
    GList *commands, *command;
    const gchar *local_codeset;
    gboolean remote, zmp_enabled;
    gsize bytes_read, bytes_written;
    gchar *encoding, *conv_text;

    GError *error = NULL;

    g_return_if_fail(IS_MUD_CONNECTION_VIEW(view));

    if(mud_connection_is_connected(view->conn))
    {
        if(view->local_echo) // Prevent password from being cached.
        {
            gchar *head = g_queue_peek_head(view->priv->history);

            if( (head && !g_str_equal(head, data)) ||
                 g_queue_is_empty(view->priv->history))
            {
                gchar *history_item = g_strdup(data);
                g_strstrip(history_item);

                /* Don't queue empty lines */
                if(strlen(history_item) != 0) 
                    g_queue_push_head(view->priv->history,
                                      history_item);
                else
                    g_free(history_item);
            }
        }
        else
            g_queue_push_head(view->priv->history,
                    (gpointer)g_strdup(_("<password removed>")));

        view->priv->current_history_index = -1;

        remote = g_settings_get_boolean(view->profile->settings, "remote-encoding");

        if(view->remote_encode && remote)
            encoding = view->remote_encoding;
        else
            encoding = g_settings_get_string(view->profile->settings, "encoding"); /* FIXME: This appears to leak */

        g_get_charset(&local_codeset);

        commands = NULL;

        if(strlen(view->profile->preferences->CommDev) == 0)
            commands = g_list_append(commands, g_strdup(data));
        else
        {
            gint i;
            gint command_array_length;
            gchar **command_array =
                g_strsplit(data, view->profile->preferences->CommDev, -1);

            command_array_length = g_strv_length(command_array);

            for(i = 0; i < command_array_length; ++i)
                commands = g_list_append(commands, g_strdup(command_array[i]));

            g_strfreev(command_array);
        }

        for (command = g_list_first(commands); command != NULL; command = command->next)
        {
            gchar *text = (gchar *)command->data;
            g_strstrip(text);

            conv_text = g_convert(text, -1,
                encoding,
                local_codeset, 
                &bytes_read, &bytes_written, &error);

            if(error)
            {
                conv_text = NULL;
                error = NULL;
            }

            zmp_handler = MUD_TELNET_ZMP(mud_telnet_get_handler(view->telnet,
                                                                TELOPT_ZMP));
            if(!zmp_handler)
                zmp_enabled = FALSE;
            else
                g_object_get(zmp_handler, "enabled", &zmp_enabled, NULL);

            if(TRUE) // FIXME: (!zmp_enabled)
            {
                const gchar *line = (conv_text == NULL) ? text : conv_text;
                mud_connection_send(view->conn, line, -1);
                mud_connection_send(view->conn, "\r\n", 2);
            }
            else // ZMP is enabled, use zmp.input.
            {
                gchar *line = (conv_text == NULL) ? text : conv_text;

                mud_zmp_send_command(zmp_handler, 2,
                                     "zmp.input",
                                     line);
            }

            if (view->profile->preferences->EchoText && view->local_echo)
            {
                mud_connection_view_add_text(view, text, Sent);
                mud_connection_view_add_text(view, "\r\n", Sent);
            }

            if(view->local_echo)
            {
                gboolean log_input;

                if(mud_log_islogging(view->log))
                {
                    g_object_get(view->log, "log-input", &log_input, NULL);

                    if(log_input)
                    {
                        mud_log_write_hook(view->log, text, strlen(text));
                        mud_log_write_hook(view->log, "\r\n", 2);
                    }
                }
            }

            if(conv_text != NULL)
                g_free(conv_text);
            g_free(text);
        }

        g_list_free(commands);
    }
}

void
mud_connection_view_start_logging(MudConnectionView *view)
{
    g_return_if_fail(IS_MUD_CONNECTION_VIEW(view));

    if(!view->logging)
    {
        mud_log_open(view->log);

        if(mud_log_islogging(view->log))
            view->logging = TRUE;
    }
}

void
mud_connection_view_stop_logging(MudConnectionView *view)
{
    g_return_if_fail(IS_MUD_CONNECTION_VIEW(view));

    if(view->logging)
    {
        view->logging = FALSE;
        mud_log_close(view->log);
    }
}

void
mud_connection_view_set_profile(MudConnectionView *view, MudProfile *profile)
{
    g_return_if_fail(IS_MUD_CONNECTION_VIEW(view));

    if(profile == view->profile) /* TODO: This bails out in case we have profile set from CONSTRUCT_ONLY, but we do want the rest done after terminal widget is ready... */
        return;

    if (view->profile)
    {
        g_signal_handler_disconnect(view->profile, view->priv->signal_profile_changed);
        g_object_unref(G_OBJECT(view->profile));
    }

    view->profile = profile;
    g_object_ref(G_OBJECT(profile));
    view->priv->signal_profile_changed =
        g_signal_connect(G_OBJECT(view->profile), "changed",
                         G_CALLBACK(mud_connection_view_profile_changed_cb),
                         view);
    mud_connection_view_reread_profile(view);
}

const gchar *
mud_connection_view_get_history_item(MudConnectionView *view, enum
                                     MudConnectionHistoryDirection direction)
{
    gchar *history_item;

    if(direction == HISTORY_DOWN)
        if( !(view->priv->current_history_index <= 0) )
            view->priv->current_history_index--;

    if(direction == HISTORY_UP)
        if(view->priv->current_history_index < 
                (gint)g_queue_get_length(view->priv->history) - 1)
            view->priv->current_history_index++;

    history_item = (gchar *)g_queue_peek_nth(view->priv->history,
            view->priv->current_history_index);

    return history_item;
}

/* MSP Download Code */
#ifdef ENABLE_GST
static void
mud_connection_view_start_download(MudConnectionView *view)
{
    MudMSPDownloadItem *item;
    GFile *source;
    GFile *destination;
    gchar **uri;
    GString *dl_label_text;

    g_return_if_fail(IS_MUD_CONNECTION_VIEW(view));
    g_return_if_fail(!view->priv->downloading);

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(view->priv->progressbar), 0.0);
    gtk_label_set_text(GTK_LABEL(view->priv->dl_label), _("Connecting…"));
    gtk_widget_show(view->priv->progressbar);
    gtk_widget_show(view->priv->dl_label);
    gtk_widget_show(view->priv->dl_button);

    item = g_queue_peek_head(view->priv->download_queue);

    uri = g_strsplit(item->url, "/", 0);

    dl_label_text = g_string_new(NULL);

    g_string_append(dl_label_text, _("Downloading"));
    g_string_append_c(dl_label_text, ' ');
    g_string_append(dl_label_text, uri[g_strv_length(uri) - 1]);
    g_string_append(dl_label_text, "…");

    gtk_label_set_text(GTK_LABEL(view->priv->dl_label), dl_label_text->str);

    g_string_free(dl_label_text, TRUE);
    g_strfreev(uri);

    source = g_file_new_for_uri(item->url);
    destination = g_file_new_for_path(item->file);

    view->priv->downloading = TRUE;
    view->priv->download_cancel = g_cancellable_new();

    g_file_copy_async(source, destination, G_FILE_COPY_NONE, G_PRIORITY_DEFAULT,
                      view->priv->download_cancel, mud_connection_view_download_progress_cb, view,
                      mud_connection_view_download_complete_cb, view);
}

void
mud_connection_view_queue_download(MudConnectionView *view, gchar *url, gchar *file)
{
    MudMSPDownloadItem *item;
    guint i, size;
    gboolean download;

    g_return_if_fail(IS_MUD_CONNECTION_VIEW(view));

    download = g_settings_get_boolean(view->profile->settings, "remote-download");

    if(download)
    {
        size = g_queue_get_length(view->priv->download_queue);

        for(i = 0; i < size; ++i) // Don't add items twice.
        {
            item = (MudMSPDownloadItem *)g_queue_peek_nth(view->priv->download_queue, i);

            if(strcmp(item->url, url) == 0)
            {
                return;
            }
        }

        item = NULL;
        item = g_malloc(sizeof(MudMSPDownloadItem));

        item->url = g_strdup(url);
        item->file = g_strdup(file);

        g_queue_push_tail(view->priv->download_queue, item);

        item = NULL;

        if(view->priv->downloading == FALSE)
            mud_connection_view_start_download(view);
    }
}

static void
mud_connection_view_download_progress_cb(goffset current_num_bytes,
                                         goffset total_num_bytes,
                                         gpointer user_data)
{
    MudConnectionView *self = MUD_CONNECTION_VIEW(user_data);

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(self->priv->progressbar),
                                  (gdouble)current_num_bytes / total_num_bytes);
}

static void
mud_connection_view_download_complete_cb(GObject *source_object,
                                         GAsyncResult *res, gpointer user_data)
{
    MudConnectionView *self = MUD_CONNECTION_VIEW(user_data);
    GFile *file = G_FILE(source_object);
    MudMSPDownloadItem *item;

    gtk_widget_hide(self->priv->progressbar);
    gtk_widget_hide(self->priv->dl_label);
    gtk_widget_hide(self->priv->dl_button);

    item = g_queue_pop_head(self->priv->download_queue);
    mud_telnet_msp_download_item_free(item);

    self->priv->downloading = FALSE;
    g_clear_object(&self->priv->download_cancel);

    if(!g_file_copy_finish(file, res, NULL))
        g_warning(_("There was an internal http connection error."));

    if(!g_queue_is_empty(self->priv->download_queue))
        mud_connection_view_start_download(self);
}

static void
mud_connection_view_download_cancel_cb(GtkWidget *widget,
                                       MudConnectionView *self)
{
    g_assert(IS_MUD_CONNECTION_VIEW(self));
    g_assert(self->priv->download_cancel);

    g_cancellable_cancel(self->priv->download_cancel);

}
#endif

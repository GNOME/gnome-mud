/* GNOME-Mud - A simple Mud Client
 * mud-subwindow.c
 * Copyright (C) 2005-2009 Les Harris <lharris@gnome.org>
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

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <vte/vte.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gprintf.h>

#include "gnome-mud.h"
#include "mud-connection-view.h"
#include "mud-window.h"
#include "mud-subwindow.h"

struct _MudSubwindowPrivate
{
    gchar *title;
    gchar *identifier;

    guint width;
    guint height;
    guint old_width;
    guint old_height;
    guint initial_width;
    guint initial_height;

    gboolean visible;
    gboolean view_hidden;
    gboolean input_enabled;
    gboolean scroll;

    GQueue *history;
    gint current_history_index;

    gint pixel_width;
    gint pixel_height;

    GtkWidget *window;
    GtkWidget *entry;
    GtkWidget *terminal;
    GtkWidget *scrollbar;
    GtkWidget *vbox;

    gint x, y;

    MudConnectionView *parent_view;
};

enum MudSubwindowHistoryDirection
{
    SUBWINDOW_HISTORY_UP,
    SUBWINDOW_HISTORY_DOWN
};

/* Property Identifiers */
enum
{
    PROP_MUD_SUBWINDOW_0,
    PROP_PARENT,
    PROP_TITLE,
    PROP_IDENT,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_VISIBLE,
    PROP_VIEW_HIDDEN,
    PROP_INPUT,
    PROP_OLD_WIDTH,
    PROP_OLD_HEIGHT,
    PROP_SCROLL
};

/* Signal Indices */
enum
{
    RESIZED,
    INPUT,
    LAST_SIGNAL
};

/* Signal Identifier Map */
static guint mud_subwindow_signal[LAST_SIGNAL] = { 0, 0 };

/* Create the Type */
G_DEFINE_TYPE(MudSubwindow, mud_subwindow, G_TYPE_OBJECT);

/* Class Functions */
static void mud_subwindow_init (MudSubwindow *self);
static void mud_subwindow_class_init (MudSubwindowClass *klass);
static void mud_subwindow_finalize (GObject *object);
static void mud_subwindow_constructed(GObject *object);
static GObject *mud_subwindow_constructor (GType gtype,
                                           guint n_properties,
                                           GObjectConstructParam *properties);
static void mud_subwindow_set_property(GObject *object,
                                       guint prop_id,
                                       const GValue *value,
                                       GParamSpec *pspec);
static void mud_subwindow_get_property(GObject *object,
                                       guint prop_id,
                                       GValue *value,
                                       GParamSpec *pspec);

/* Callback Functions */
static gboolean mud_subwindow_delete_event_cb(GtkWidget *widget,
		                   	      GdkEvent *event,
			                      MudSubwindow *self);

static gboolean mud_subwindow_entry_keypress_cb(GtkWidget *widget,
                                                GdkEventKey *event,
                                                MudSubwindow *self);

static gboolean mud_subwindow_configure_event_cb(GtkWidget *widget,
                                                 GdkEventConfigure *event,
                                                 gpointer user_data);

static void mud_subwindow_size_allocate_cb(GtkWidget *widget,
                                           GtkAllocation *allocation,
                                           MudSubwindow *self);

static gboolean mud_subwindow_focus_in_cb(GtkWidget *widget,
                                          GdkEventFocus *event,
                                          gpointer user_data);

/* Private Methods */
static void mud_subwindow_set_size_force_grid (MudSubwindow *window,
                                               VteTerminal *screen,
                                               gboolean        even_if_mapped,
                                               int             force_grid_width,
                                               int             force_grid_height);

const gchar *mud_subwindow_get_history_item(MudSubwindow *self,
                                            enum MudSubwindowHistoryDirection direction);

static void mud_subwindow_set_terminal_colors(MudSubwindow *self);
static void mud_subwindow_set_terminal_scrollback(MudSubwindow *self);
static void mud_subwindow_set_terminal_scrolloutput(MudSubwindow *self);
static void mud_subwindow_set_terminal_font(MudSubwindow *self);

/* MudSubwindow class functions */
static void
mud_subwindow_class_init (MudSubwindowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    /* Override base object constructor */
    object_class->constructor = mud_subwindow_constructor;

    object_class->constructed = mud_subwindow_constructed;

    /* Override base object's finalize */
    object_class->finalize = mud_subwindow_finalize;

    /* Override base object property methods */
    object_class->set_property = mud_subwindow_set_property;
    object_class->get_property = mud_subwindow_get_property;

    /* Add private data to class */
    g_type_class_add_private(klass, sizeof(MudSubwindowPrivate));

    /* Install Properties */
    g_object_class_install_property(object_class,
            PROP_PARENT,
            g_param_spec_object("parent-view",
                "Parent View",
                "The parent MudSubwindow",
                MUD_TYPE_CONNECTION_VIEW,
                G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property(object_class,
            PROP_TITLE,
            g_param_spec_string("title",
                "Title",
                "The visible Title of the subwindow.",
                NULL,
                G_PARAM_READWRITE|G_PARAM_CONSTRUCT));

    g_object_class_install_property(object_class,
            PROP_IDENT,
            g_param_spec_string("identifier",
                "Ident",
                "The identifier of the subwindow.",
                NULL,
                G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property(object_class,
            PROP_WIDTH,
            g_param_spec_uint("width",
                "Width",
                "The width of the terminal in columns.",
                1,
                1024,
                40,
                G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property(object_class,
            PROP_HEIGHT,
            g_param_spec_uint("height",
                "Height",
                "The height of the terminal in rows.",
                1,
                1024,
                40,
                G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property(object_class,
            PROP_OLD_HEIGHT,
            g_param_spec_uint("old-height",
                "Old Height",
                "The old height of the terminal in rows.",
                0,
                1024,
                0,
                G_PARAM_READWRITE));

    g_object_class_install_property(object_class,
            PROP_OLD_WIDTH,
            g_param_spec_uint("old-width",
                "Old Width",
                "The old width of the terminal in rows.",
                0,
                1024,
                0,
                G_PARAM_READWRITE));

    g_object_class_install_property(object_class,
            PROP_VISIBLE,
            g_param_spec_boolean("visible",
                "Visible",
                "True if subwindow is visible.",
                TRUE,
                G_PARAM_READWRITE));

    g_object_class_install_property(object_class,
            PROP_VIEW_HIDDEN,
            g_param_spec_boolean("view-hidden",
                "View Hidden",
                "True if subwindow is hidden by the view.",
                FALSE,
                G_PARAM_READWRITE));

    g_object_class_install_property(object_class,
            PROP_INPUT,
            g_param_spec_boolean("input-enabled",
                "Input Enabled",
                "True if subwindow accepts input.",
                FALSE,
                G_PARAM_READWRITE));

    g_object_class_install_property(object_class,
            PROP_SCROLL,
            g_param_spec_boolean("scroll-enabled",
                "Scroll Enabled",
                "True if subwindow scrolls.",
                TRUE,
                G_PARAM_READWRITE|G_PARAM_CONSTRUCT));

    /* Register Signals */
    mud_subwindow_signal[RESIZED] =
        g_signal_new("resized",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST | G_SIGNAL_NO_HOOKS,
                     0,
                     NULL,
                     NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE,
                     0);

    mud_subwindow_signal[INPUT] =
        g_signal_new("input-received",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST | G_SIGNAL_NO_HOOKS,
                     0,
                     NULL,
                     NULL,
                     g_cclosure_marshal_VOID__STRING,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_STRING);

}

static void
mud_subwindow_init (MudSubwindow *self)
{
    /* Get our private data */
    self->priv = MUD_SUBWINDOW_GET_PRIVATE(self);

    /* Set Defaults */
    self->priv->parent_view = NULL;
    self->priv->title = NULL;
    self->priv->identifier = NULL;
    self->priv->visible = TRUE;
    self->priv->view_hidden = FALSE;
    self->priv->input_enabled = FALSE;
    self->priv->history = g_queue_new();
    self->priv->current_history_index = 0;
    self->priv->width = 0;
    self->priv->height = 0;
    self->priv->old_width = 0;
    self->priv->old_height = 0;
    self->priv->initial_width = 0;
    self->priv->initial_height = 0;
    self->priv->x = 0;
    self->priv->y = 0;

    self->priv->window = NULL;
    self->priv->entry = NULL;
    self->priv->scrollbar = NULL;
    self->priv->terminal = NULL;
    self->priv->vbox = NULL;
}

static GObject *
mud_subwindow_constructor (GType gtype,
                           guint n_properties,
                           GObjectConstructParam *properties)
{
    GtkWidget *term_box;
    MudWindow *app;
    GtkWidget *main_window;

    MudSubwindow *self;
    GObject *obj;
    MudSubwindowClass *klass;
    GObjectClass *parent_class;
    GtkBuilder *builder;

    /* Chain up to parent constructor */
    klass = MUD_SUBWINDOW_CLASS( g_type_class_peek(MUD_TYPE_SUBWINDOW) );
    parent_class = G_OBJECT_CLASS( g_type_class_peek_parent(klass) );
    obj = parent_class->constructor(gtype, n_properties, properties);

    self = MUD_SUBWINDOW(obj);

    if(!self->priv->parent_view)
    {
        g_printf("ERROR: Tried to instantiate MudSubwindow without passing parent view\n");
        g_error("Tried to instantiate MudSubwindow without passing parent view");
    }

    if(!self->priv->title)
    {
        g_printf("ERROR: Tried to instantiate MudSubwindow without passing title\n");
        g_error("Tried to instantiate MudSubwindow without passing title.");
    }

    if(!self->priv->identifier)
    {
        g_printf("ERROR: Tried to instantiate MudSubwindow without passing identifier\n");
        g_error("Tried to instantiate MudSubwindow without passing identifier.");
    }

    if(self->priv->width == 0 || self->priv->height == 0)
    {
        g_printf("ERROR: Tried to instantiate MudSubwindow without passing valid width/height\n");
        g_error("Tried to instantiate MudSubwindow without passing valid width/height.");
    }

    self->priv->old_width = self->priv->width;
    self->priv->old_height = self->priv->height;
    self->priv->initial_width = self->priv->width;
    self->priv->initial_height = self->priv->height;

    /* start glading */
    builder = gtk_builder_new_from_resource ("/org/gnome/MUD/main.ui");

    self->priv->window = GTK_WIDGET(gtk_builder_get_object(builder, "subwindow"));

    g_object_unref(builder);

    gtk_window_set_type_hint(GTK_WINDOW(self->priv->window),
                             GDK_WINDOW_TYPE_HINT_UTILITY);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(self->priv->window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(self->priv->window), TRUE);

    g_object_get(self->priv->parent_view, "window", &app, NULL);
    g_object_get(app, "window", &main_window, NULL);

    gtk_window_set_transient_for(GTK_WINDOW(self->priv->window),
                                 GTK_WINDOW(main_window));

    self->priv->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    self->priv->entry = gtk_entry_new();

    gtk_widget_hide(self->priv->entry);

    self->priv->terminal = vte_terminal_new();
    self->priv->scrollbar = gtk_scrollbar_new (GTK_ORIENTATION_VERTICAL, gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (self->priv->terminal)));
    term_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    vte_terminal_set_encoding(VTE_TERMINAL(self->priv->terminal),
                              "ISO-8859-1", NULL); /* TODO: This is deprecated; if keeping, at least add error handling? */

    /* TODO: set_emulation doesn't exist anymore. We don't really care, but does it affect TTYPE queries? */
    /* vte_terminal_set_emulation(VTE_TERMINAL(self->priv->terminal), "xterm"); */

    vte_terminal_set_cursor_shape(VTE_TERMINAL(self->priv->terminal),
                                  VTE_CURSOR_SHAPE_UNDERLINE);

    vte_terminal_set_cursor_blink_mode(VTE_TERMINAL(self->priv->terminal),
                                       VTE_CURSOR_BLINK_OFF);

    vte_terminal_set_size(VTE_TERMINAL(self->priv->terminal),
                                       self->priv->initial_width,
                                       self->priv->initial_height);

    gtk_box_pack_start(GTK_BOX(term_box),
                       self->priv->terminal,
                       TRUE,
                       TRUE,
                       0);

    gtk_box_pack_end(GTK_BOX(term_box),
                     self->priv->scrollbar,
                     FALSE,
                     FALSE,
                     0);

    gtk_box_pack_start(GTK_BOX(self->priv->vbox), term_box, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(self->priv->vbox), self->priv->entry, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(self->priv->window), self->priv->vbox);

    gtk_window_set_title(GTK_WINDOW(self->priv->window), self->priv->title);

    gtk_widget_show(term_box);
    gtk_widget_show(self->priv->vbox);

    if(self->priv->scroll)
        gtk_widget_show(self->priv->scrollbar);

    gtk_widget_show(self->priv->terminal);
    gtk_widget_show(self->priv->window);

    gtk_window_get_size(GTK_WINDOW(self->priv->window),
                        &self->priv->pixel_width,
                        &self->priv->pixel_height);


    return obj;
}

static void
mud_subwindow_constructed(GObject *object)
{
    MudSubwindow *self = MUD_SUBWINDOW(object);

    mud_subwindow_reread_profile(self);

    vte_terminal_set_size(VTE_TERMINAL(self->priv->terminal),
                          self->priv->initial_width,
                          self->priv->initial_height);

    mud_subwindow_set_size_force_grid(self,
                                      VTE_TERMINAL(self->priv->terminal),
                                      TRUE,
                                      self->priv->initial_width,
                                      self->priv->initial_height);

    g_signal_connect(self->priv->window,
                     "delete-event",
                     G_CALLBACK(mud_subwindow_delete_event_cb),
                     self);

    g_signal_connect(self->priv->window,
                     "configure-event",
                     G_CALLBACK(mud_subwindow_configure_event_cb),
                     self);

    g_signal_connect(self->priv->window,
                     "size-allocate",
                     G_CALLBACK(mud_subwindow_size_allocate_cb),
                     self);

    g_signal_connect(self->priv->entry,
                     "key_press_event",
                     G_CALLBACK(mud_subwindow_entry_keypress_cb),
                     self);

    g_signal_connect(self->priv->terminal,
                     "focus-in-event",
                     G_CALLBACK(mud_subwindow_focus_in_cb),
                     self);
}

static void
mud_subwindow_finalize (GObject *object)
{
    MudSubwindow *self;
    GObjectClass *parent_class;
    gchar *history_item;

    self = MUD_SUBWINDOW(object);

    if(self->priv->history && !g_queue_is_empty(self->priv->history))
        while((history_item = (gchar *)g_queue_pop_head(self->priv->history)) != NULL)
            g_free(history_item);

    if(self->priv->history)
        g_queue_free(self->priv->history);

    gtk_widget_destroy(self->priv->window);

    parent_class = g_type_class_peek_parent(G_OBJECT_GET_CLASS(object));
    parent_class->finalize(object);
}

static void
mud_subwindow_set_property(GObject *object,
                      guint prop_id,
                      const GValue *value,
                      GParamSpec *pspec)
{
    MudSubwindow *self;
    gchar *new_string;
    gboolean new_boolean;
    guint new_uint;

    self = MUD_SUBWINDOW(object);

    switch(prop_id)
    {
        case PROP_INPUT:
            new_boolean = g_value_get_boolean(value);

            if(new_boolean != self->priv->input_enabled)
                self->priv->input_enabled = new_boolean;
            break;

        case PROP_SCROLL:
            new_boolean = g_value_get_boolean(value);

            if(new_boolean != self->priv->scroll)
                self->priv->scroll = new_boolean;
            break;

        case PROP_VISIBLE:
            new_boolean = g_value_get_boolean(value);

            if(new_boolean != self->priv->visible)
                self->priv->visible = new_boolean;
            break;

        case PROP_VIEW_HIDDEN:
            new_boolean = g_value_get_boolean(value);

            if(new_boolean != self->priv->view_hidden)
                self->priv->view_hidden = new_boolean;
            break;

        case PROP_HEIGHT:
            new_uint = g_value_get_uint(value);

            if(new_uint != self->priv->height)
                self->priv->height = new_uint;
            break;

        case PROP_WIDTH:
            new_uint = g_value_get_uint(value);

            if(new_uint != self->priv->width)
                self->priv->width = new_uint;
            break;

        case PROP_OLD_HEIGHT:
            new_uint = g_value_get_uint(value);

            if(new_uint != self->priv->old_height)
                self->priv->old_height = new_uint;
            break;

        case PROP_OLD_WIDTH:
            new_uint = g_value_get_uint(value);

            if(new_uint != self->priv->old_width)
                self->priv->old_width = new_uint;
            break;

        case PROP_IDENT:
            new_string = g_value_dup_string(value);

            if(!self->priv->identifier)
                self->priv->identifier = g_strdup(new_string);
            else if(!g_str_equal(self->priv->identifier, new_string))
            {
                g_free(self->priv->identifier);
                self->priv->identifier = g_strdup(new_string);
            }

            g_free(new_string);

            break;

        case PROP_TITLE:
            new_string = g_value_dup_string(value);

            if(!self->priv->title)
                self->priv->title = g_strdup(new_string);
            else if(!g_str_equal(self->priv->title, new_string))
            {
                g_free(self->priv->title);
                self->priv->title = g_strdup(new_string);
            }

            g_free(new_string);

            break;

        case PROP_PARENT:
            self->priv->parent_view = MUD_CONNECTION_VIEW(g_value_get_object(value));
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void
mud_subwindow_get_property(GObject *object,
                      guint prop_id,
                      GValue *value,
                      GParamSpec *pspec)
{
    MudSubwindow *self;

    self = MUD_SUBWINDOW(object);

    switch(prop_id)
    {
        case PROP_TITLE:
            g_value_set_string(value, self->priv->title);
            break;

        case PROP_IDENT:
            g_value_set_string(value, self->priv->identifier);
            break;

        case PROP_SCROLL:
            g_value_set_boolean(value, self->priv->scroll);
            break;

        case PROP_WIDTH:
            g_value_set_uint(value, self->priv->width);
            break;

        case PROP_HEIGHT:
            g_value_set_uint(value, self->priv->height);
            break;

        case PROP_OLD_HEIGHT:
            g_value_set_uint(value, self->priv->old_height);
            break;

        case PROP_OLD_WIDTH:
            g_value_set_uint(value, self->priv->old_width);
            break;

        case PROP_VISIBLE:
            g_value_set_boolean(value, self->priv->visible);
            break;

        case PROP_VIEW_HIDDEN:
            g_value_set_boolean(value, self->priv->view_hidden);
            break;

        case PROP_INPUT:
            g_value_set_boolean(value, self->priv->input_enabled);
            break;

        case PROP_PARENT:
            g_value_take_object(value, self->priv->parent_view);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

/* Private Methods */
const gchar *
mud_subwindow_get_history_item(MudSubwindow *self,
                               enum MudSubwindowHistoryDirection direction)
{
    gchar *history_item;

    if(direction == SUBWINDOW_HISTORY_DOWN)
        if( !(self->priv->current_history_index <= 0) )
            self->priv->current_history_index--;

    if(direction == SUBWINDOW_HISTORY_UP)
        if(self->priv->current_history_index <
                (gint)g_queue_get_length(self->priv->history) - 1)
            self->priv->current_history_index++;

    history_item = (gchar *)g_queue_peek_nth(self->priv->history,
            self->priv->current_history_index);

    return history_item;
}

static void
mud_subwindow_set_terminal_colors(MudSubwindow *self)
{
    g_return_if_fail(MUD_IS_SUBWINDOW(self));

    vte_terminal_set_colors(VTE_TERMINAL(self->priv->terminal),
            &self->priv->parent_view->profile->preferences->Foreground,
            &self->priv->parent_view->profile->preferences->Background,
            self->priv->parent_view->profile->preferences->Colors, C_MAX);

    vte_terminal_set_color_cursor(VTE_TERMINAL(self->priv->terminal),
            &self->priv->parent_view->profile->preferences->Background);
}

static void
mud_subwindow_set_terminal_scrollback(MudSubwindow *self)
{
    g_return_if_fail(MUD_IS_SUBWINDOW(self));

    vte_terminal_set_scrollback_lines(VTE_TERMINAL(self->priv->terminal),
            self->priv->parent_view->profile->preferences->Scrollback);
}

static void
mud_subwindow_set_terminal_scrolloutput(MudSubwindow *self)
{
    g_return_if_fail(MUD_IS_SUBWINDOW(self));

    if(self->priv->terminal)
        vte_terminal_set_scroll_on_output(VTE_TERMINAL(self->priv->terminal),
                self->priv->parent_view->profile->preferences->ScrollOnOutput);
}

static void
mud_subwindow_set_terminal_font(MudSubwindow *self)
{
    PangoFontDescription *desc = NULL;
    char *name;

    g_return_if_fail(MUD_IS_SUBWINDOW(self));

    name = self->priv->parent_view->profile->preferences->FontName;

    if(name)
        desc = pango_font_description_from_string(name);

    if(!desc)
        desc = pango_font_description_from_string("Monospace 10");

    vte_terminal_set_font(VTE_TERMINAL(self->priv->terminal), desc);

    mud_subwindow_set_size_force_grid(self,
            VTE_TERMINAL(self->priv->terminal),
            TRUE,
            self->priv->width,
            self->priv->height);
}

static void
mud_subwindow_set_size_force_grid (MudSubwindow *window,
                                   VteTerminal *screen,
                                   gboolean        even_if_mapped,
                                   int             force_grid_width,
                                   int             force_grid_height)
{
    /* TODO: Missing get_padding in new VTE; maybe we can just use vte_terminal_set_size? */
#if 0
#warning Reimplement mud_subwindow size forcing
    /* Owen's hack from gnome-terminal */
    GtkWidget *widget;
    GtkWidget *app;
    GtkRequisition toplevel_request;
    GtkRequisition widget_request;
    int w, h;
    int char_width;
    int char_height;
    int grid_width;
    int grid_height;
    int xpad;
    int ypad;

    g_return_if_fail(MUD_IS_SUBWINDOW(window));

    /* be sure our geometry is up-to-date */
    mud_subwindow_update_geometry (window);
    widget = GTK_WIDGET (screen);

    app = window->priv->window;

    gtk_widget_size_request (app, &toplevel_request);
    gtk_widget_size_request (widget, &widget_request);

    w = toplevel_request.width - widget_request.width;
    h = toplevel_request.height - widget_request.height;

    char_width = vte_terminal_get_char_width (screen);
    char_height = vte_terminal_get_char_height (screen);

    grid_width = vte_terminal_get_column_count (screen);
    grid_height = vte_terminal_get_row_count (screen);

    if (force_grid_width >= 0)
        grid_width = force_grid_width;
    if (force_grid_height >= 0)
        grid_height = force_grid_height;

    vte_terminal_get_padding (VTE_TERMINAL (screen), &xpad, &ypad);

    w += xpad * 2 + char_width * grid_width;
    h += ypad * 2 + char_height * grid_height;

    if (even_if_mapped && gtk_widget_get_mapped (app)) {
        gtk_window_resize (GTK_WINDOW (app), w, h);
    }
}

static void
mud_subwindow_update_geometry (MudSubwindow *window)
{
    GtkWidget *widget = window->priv->terminal;
    GdkGeometry hints;
    gint char_width;
    gint char_height;
    gint xpad, ypad;

    if(gtk_widget_get_mapped(window->priv->window))
    {
        char_width = vte_terminal_get_char_width (VTE_TERMINAL(widget));
        char_height = vte_terminal_get_char_height (VTE_TERMINAL(widget));

        vte_terminal_get_padding (VTE_TERMINAL (window->priv->terminal), &xpad, &ypad);

        hints.base_width = xpad;
        hints.base_height = ypad;

#define MIN_WIDTH_CHARS 4
#define MIN_HEIGHT_CHARS 2

        hints.width_inc = char_width;
        hints.height_inc = char_height;

        /* min size is min size of just the geometry widget, remember. */
        hints.min_width = hints.base_width + hints.width_inc * MIN_WIDTH_CHARS;
        hints.min_height = hints.base_height + hints.height_inc * MIN_HEIGHT_CHARS;

        gtk_window_set_geometry_hints (GTK_WINDOW (window->priv->window),
                widget,
                &hints,
                GDK_HINT_RESIZE_INC |
                GDK_HINT_MIN_SIZE |
                GDK_HINT_BASE_SIZE);
    }
#endif
}

/* MudSubwindow Callbacks */
static gboolean
mud_subwindow_delete_event_cb(GtkWidget *widget,
			      GdkEvent *event,
			      MudSubwindow *self)
{
    GdkWindow *wm_window;
    GdkRectangle rect;

    wm_window = gtk_widget_get_window(self->priv->window);
    gdk_window_get_frame_extents(wm_window, &rect);

    self->priv->x = rect.x;
    self->priv->y = rect.y;

    gtk_widget_hide(self->priv->window);
    self->priv->visible = FALSE;
    self->priv->view_hidden = FALSE;

    return TRUE;
}

static gboolean
mud_subwindow_configure_event_cb(GtkWidget *widget,
                                 GdkEventConfigure *event,
                                 gpointer user_data)
{
    MudSubwindow *self = MUD_SUBWINDOW(user_data);

    if(gtk_widget_get_mapped(self->priv->entry))
        gtk_widget_grab_focus(self->priv->entry);

    return FALSE;
}

static void
mud_subwindow_size_allocate_cb(GtkWidget *widget,
                               GtkAllocation *allocation,
                               MudSubwindow *self)
{
    if(gtk_widget_get_mapped(self->priv->window))
    {
        if(self->priv->width != allocation->width ||
                self->priv->height != allocation->height)
        {
            self->priv->width = vte_terminal_get_column_count (VTE_TERMINAL (self->priv->terminal));
            self->priv->height = vte_terminal_get_row_count (VTE_TERMINAL(self->priv->terminal));

            g_signal_emit(self,
                    mud_subwindow_signal[RESIZED],
                    0,
                    self->priv->width,
                    self->priv->height);
        }
    }
}

static gboolean
mud_subwindow_focus_in_cb(GtkWidget *widget,
                          GdkEventFocus *event,
                          gpointer user_data)
{
    MudSubwindow *self = MUD_SUBWINDOW(user_data);

    if(gtk_widget_get_mapped(self->priv->entry))
        gtk_widget_grab_focus(self->priv->entry);

    return TRUE;
}

static gboolean
mud_subwindow_entry_keypress_cb(GtkWidget *widget,
                                GdkEventKey *event,
                                MudSubwindow *self)
{
    const gchar *history;

    if ((event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) &&
        (event->state & gtk_accelerator_get_default_mod_mask()) == 0   &&
         gtk_widget_get_mapped(self->priv->entry) )
    {
        gchar *head = g_queue_peek_head(self->priv->history);
        const gchar *text = gtk_entry_get_text(GTK_ENTRY(self->priv->entry));

        if( (head && !g_str_equal(head, text)) ||
             g_queue_is_empty(self->priv->history))
        {
            gchar *history_item = g_strdup(text);
            g_strstrip(history_item);

            /* Don't queue empty lines */
            if(strlen(history_item) != 0)
                g_queue_push_head(self->priv->history,
                                  history_item);
            else
                g_free(history_item);
        }

        self->priv->current_history_index = -1;

        g_signal_emit(self,
                      mud_subwindow_signal[INPUT],
                      0,
                      text);

        if (g_settings_get_boolean(self->priv->parent_view->profile->settings, "keep-text"))
            gtk_editable_select_region(GTK_EDITABLE(self->priv->entry), 0, -1);
        else
            gtk_entry_set_text(GTK_ENTRY(self->priv->entry), "");

        return TRUE;
    }

    if(event->keyval == GDK_KEY_Up)
    {
        history =
            mud_subwindow_get_history_item(self,
                                           SUBWINDOW_HISTORY_UP);

        if(history)
        {
            gtk_entry_set_text(GTK_ENTRY(self->priv->entry), history);
            gtk_editable_select_region(GTK_EDITABLE(self->priv->entry), 0, -1);
        }

        return TRUE;
    }

    if(event->keyval == GDK_KEY_Down)
    {
        history =
            mud_subwindow_get_history_item(self,
                                           SUBWINDOW_HISTORY_DOWN);

        if(history)
        {
            gtk_entry_set_text(GTK_ENTRY(self->priv->entry), history);
            gtk_editable_select_region(GTK_EDITABLE(self->priv->entry), 0, -1);
        }

        return TRUE;
    }

    return FALSE;
}

/* Public Methods */
void
mud_subwindow_set_size(MudSubwindow *self,
                       guint width,
                       guint height)
{
    g_return_if_fail(MUD_IS_SUBWINDOW(self));

    self->priv->width = width;
    self->priv->height = height;

    vte_terminal_set_size(VTE_TERMINAL(self->priv->terminal),
                          self->priv->width,
                          self->priv->height);

    mud_subwindow_set_size_force_grid(self,
                                      VTE_TERMINAL(self->priv->terminal),
                                      TRUE,
                                      self->priv->width,
                                      self->priv->height);
}

void
mud_subwindow_show(MudSubwindow *self)
{
    g_return_if_fail(MUD_IS_SUBWINDOW(self));

    if(!self->priv->view_hidden)
    {
        gtk_widget_show(self->priv->window);
        gtk_window_move(GTK_WINDOW(self->priv->window),
                self->priv->x,
                self->priv->y);
        self->priv->visible = TRUE;
    }
    else
        self->priv->view_hidden = TRUE;
}


void
mud_subwindow_hide(MudSubwindow *self)
{
    GdkWindow *wm_window;
    GdkRectangle rect;

    g_return_if_fail(MUD_IS_SUBWINDOW(self));

    if(self->priv->visible)
    {
        wm_window = gtk_widget_get_window(self->priv->window);
        gdk_window_get_frame_extents(wm_window, &rect);

        self->priv->x = rect.x;
        self->priv->y = rect.y;

        self->priv->visible = FALSE;
        gtk_widget_hide(self->priv->window);
    }
}

void
mud_subwindow_reread_profile(MudSubwindow *self)
{
    g_return_if_fail(MUD_IS_SUBWINDOW(self));

    mud_subwindow_set_terminal_colors(self);
    mud_subwindow_set_terminal_scrollback(self);
    mud_subwindow_set_terminal_scrolloutput(self);
    mud_subwindow_set_terminal_font(self);
}

void
mud_subwindow_feed(MudSubwindow *self,
                   const gchar *data,
                   guint length)
{
    g_return_if_fail(MUD_IS_SUBWINDOW(self));

    vte_terminal_feed(VTE_TERMINAL(self->priv->terminal),
                      data,
                      length);
}

void
mud_subwindow_set_title(MudSubwindow *self,
                        const gchar *title)
{
    g_return_if_fail(MUD_IS_SUBWINDOW(self));

    gtk_window_set_title(GTK_WINDOW(self->priv->window), title);
}

void
mud_subwindow_enable_input(MudSubwindow *self,
                           gboolean enable)
{
    g_return_if_fail(MUD_IS_SUBWINDOW(self));

    self->priv->input_enabled = enable;

    if(enable)
        gtk_widget_show(self->priv->entry);
    else
        gtk_widget_hide(self->priv->entry);

    mud_subwindow_set_size(self, self->priv->width, self->priv->height);
}

void
mud_subwindow_enable_scroll(MudSubwindow *self,
                           gboolean enable)
{
    g_return_if_fail(MUD_IS_SUBWINDOW(self));

    self->priv->input_enabled = enable;

    if(enable)
        gtk_widget_show(self->priv->scrollbar);
    else
        gtk_widget_hide(self->priv->scrollbar);

    mud_subwindow_set_size(self, self->priv->width, self->priv->height);
}


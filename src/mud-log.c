/* GNOME-Mud - A simple Mud Client
 * mud-log.c
 * Copyright (C) 2006-2009 Les Harris <lharris@gnome.org>
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

#include <glib/gi18n.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <glib/gprintf.h>
#include <stdlib.h>

#include "gnome-mud.h"
#include "mud-log.h"
#include "mud-window.h"
#include "mud-connection-view.h"
#include "mud-line-buffer.h"
#include "utils.h"
#include "ecma48.h"

struct _MudLogPrivate
{
    GtkWidget *window;
    GtkWidget *spin_log_next;
    GtkWidget *spin_log_prev;
    GtkWidget *check_log_next;
    GtkWidget *check_log_prev;
    GtkWidget *check_append;
    GtkWidget *check_buffer;
    GtkWidget *check_input;
    GtkWidget *check_color;
    GtkWidget *entry_file;
    GtkWidget *btn_select;

    gboolean active;
    gboolean append;
    gboolean color;
    gboolean buffer;
    gboolean include_next;
    gboolean include_prev;
    gboolean input;

    gboolean done;
    gboolean finalizing;
    gboolean bold;

    gint next_count;
    gint prev_count;

    gint count;

    gchar *dir;
    gchar *filename;

    FILE *logfile;

    GQueue *span_queue;
    GdkColor xterm_colors[256];

    MudLineBuffer *line_buffer;

    MudWindow *parent_window;
    MudConnectionView *parent;
};

/* Property Identifiers */
enum
{
    PROP_MUD_LOG_0,
    PROP_MUD_NAME,
    PROP_PARENT,
    PROP_WINDOW,
    PROP_INPUT
};

/* Define the Type */
G_DEFINE_TYPE(MudLog, mud_log, G_TYPE_OBJECT);

/* Class Functions */
static void mud_log_init (MudLog *log);
static void mud_log_class_init (MudLogClass *klass);
static void mud_log_finalize (GObject *object);
static GObject *mud_log_constructor (GType gtype,
                                     guint n_properties,
                                     GObjectConstructParam *properties);
static void mud_log_set_property(GObject *object,
                                 guint prop_id,
                                 const GValue *value,
                                 GParamSpec *pspec);
static void mud_log_get_property(GObject *object,
                                 guint prop_id,
                                 GValue *value,
                                 GParamSpec *pspec);

/* Callbacks */
static void mud_log_next_toggled_cb(GtkToggleButton *button, MudLog *self);
static void mud_log_prev_toggled_cb(GtkToggleButton *button, MudLog *self);
static void mud_log_append_toggled_cb(GtkToggleButton *button, MudLog *self);
static void mud_log_buffer_toggled_cb(GtkToggleButton *button, MudLog *self);
static void mud_log_input_toggled_cb(GtkToggleButton *button, MudLog *self);
static void mud_log_color_toggled_cb(GtkToggleButton *button, MudLog *self);
static void mud_log_select_clicked_cb(GtkWidget *widget, MudLog *self);
static void mud_log_next_spin_changed_cb(GtkSpinButton *button, MudLog *self);
static void mud_log_prev_spin_changed_cb(GtkSpinButton *button, MudLog *self);
static gboolean mud_log_keypress_cb(GtkWidget *widget,
                                    GdkEventKey *event,
                                    MudLog *self);
static void mud_log_line_added_cb(MudLineBuffer *buffer,
                                  MudLineBufferLine *line,
                                  MudLog *self);

/* Private Methods */
static void mud_log_write(MudLog *log, const gchar *data, gsize size);
static gchar *mud_log_parse_ansi(MudLog *self, const gchar *data, gsize size);
static void mud_log_parse_ecma_color(MudLog *self,
                                     const gchar *data,
                                     gsize size,
                                     GString *output);
static void mud_log_write_html_header(MudLog *self);
static void mud_log_write_html_footer(MudLog *self);
static void mud_log_write_html_foreground_span(MudLog *self,
                                               GString *output,
                                               gboolean bold,
                                               gint ecma_code);
static void mud_log_write_html_background_span(MudLog *self,
                                               GString *output,
                                               gboolean bold,
                                               gint ecma_code);
static void mud_log_write_html_xterm_span(MudLog *self,
                                          GString *output,
                                          gboolean foreground,
                                          gint color);
static void mud_log_create_xterm_colors(MudLog *self);

// MudLog class functions
static void
mud_log_class_init (MudLogClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    /* Override base object constructor */
    object_class->constructor = mud_log_constructor;

    /* Override base object's finalize */
    object_class->finalize = mud_log_finalize;

    /* Override base object property methods */
    object_class->set_property = mud_log_set_property;
    object_class->get_property = mud_log_get_property;

    /* Add private data to class */
    g_type_class_add_private(klass, sizeof(MudLogPrivate));

    /* Install Properties */
    g_object_class_install_property(object_class,
            PROP_MUD_NAME,
            g_param_spec_string("mud-name",
                "mud name",
                "name of mud we are logging",
                "Unnamed",
                G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property(object_class,
            PROP_WINDOW,
            g_param_spec_object("window",
                "window",
                "Parent MudWindow",
                MUD_TYPE_WINDOW,
                G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property(object_class,
            PROP_PARENT,
            g_param_spec_object("parent",
                "parent",
                "Parent MudConnectionView",
                MUD_TYPE_CONNECTION_VIEW,
                G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property(object_class,
            PROP_INPUT,
            g_param_spec_boolean("log-input",
                "Log Input",
                "The log should log the input as well.",
                TRUE,
                G_PARAM_READABLE));
}

static void
mud_log_init (MudLog *log)
{
    log->priv = MUD_LOG_GET_PRIVATE(log);

    /* Set defaults for Public Members */
    log->mud_name = NULL;

    /* Set defaults for Private Members */
    log->priv->active = FALSE;
    log->priv->logfile = NULL;
    log->priv->parent_window = NULL;
    log->priv->parent = NULL;
    log->priv->filename = NULL;
    log->priv->input = TRUE;

    log->priv->append = TRUE;
    log->priv->buffer = FALSE;
    log->priv->color = FALSE;
    log->priv->include_next = FALSE;
    log->priv->include_prev = FALSE;
    log->priv->done = FALSE;
    log->priv->finalizing = FALSE;
    log->priv->bold = FALSE;

    log->priv->count = 0; 
    log->priv->next_count = 0;
    log->priv->prev_count = 0;
}

static GObject *
mud_log_constructor (GType gtype,
                     guint n_properties,
                     GObjectConstructParam *properties)
{
    MudLog *self;
    GObject *obj;

    MudLogClass *klass;
    GObjectClass *parent_class;

    /* Chain up to window constructor */
    klass = MUD_LOG_CLASS( g_type_class_peek(MUD_TYPE_LOG) );
    parent_class = G_OBJECT_CLASS( g_type_class_peek_parent(klass) );
    obj = parent_class->constructor(gtype, n_properties, properties);

    self = MUD_LOG(obj);

    if(!self->mud_name)
    {
        g_printf("ERROR: Tried to instantiate MudLog without passing mud name.\n");
        g_error("Tried to instantiate MudLog without passing mud name.");
    }

    if(!self->priv->parent_window)
    {
        g_printf("ERROR: Tried to instantiate MudLog without passing MudWindow.\n");
        g_error("Tried to instantiate MudLog without passing MudWindow.");    
    }

    if(!self->priv->parent)
    {
        g_printf("ERROR: Tried to instantiate MudLog without passing MudConnectionView.\n");
        g_error("Tried to instantiate MudLog without passing MudConnectionView.");    
    }

    self->priv->span_queue = g_queue_new();
    mud_log_create_xterm_colors(self);

    return obj;
}

static void
mud_log_finalize (GObject *object)
{
    MudLog *MLog;
    GObjectClass *parent_class;

    MLog = MUD_LOG(object);

    MLog->priv->finalizing = TRUE;

    if(MLog->priv->active)
        mud_log_close(MLog);

    g_queue_free(MLog->priv->span_queue);

    if(MLog->mud_name)
        g_free(MLog->mud_name);

    parent_class = g_type_class_peek_parent(G_OBJECT_GET_CLASS(object));
    parent_class->finalize(object);
}

static void
mud_log_set_property(GObject *object,
                     guint prop_id,
                     const GValue *value,
                     GParamSpec *pspec)
{
    MudLog *self;
    
    gchar *new_mud_name;

    self = MUD_LOG(object);

    switch(prop_id)
    {
        case PROP_MUD_NAME:
            new_mud_name = g_value_dup_string(value);

            if(!self->mud_name)
                self->mud_name = g_strdup(new_mud_name);
            else if( strcmp(self->mud_name, new_mud_name) != 0)
            {
                g_free(self->mud_name);
                self->mud_name = g_strdup(new_mud_name);
            }

            g_free(new_mud_name);

            break;

        case PROP_WINDOW:
            self->priv->parent_window = MUD_WINDOW(g_value_get_object(value));
            break;

        case PROP_PARENT:
            self->priv->parent = MUD_CONNECTION_VIEW(g_value_get_object(value));
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void
mud_log_get_property(GObject *object,
                     guint prop_id,
                     GValue *value,
                     GParamSpec *pspec)
{
    MudLog *self;

    self = MUD_LOG(object);

    switch(prop_id)
    {
        case PROP_MUD_NAME:
            g_value_set_string(value, self->mud_name);
            break;

        case PROP_WINDOW:
            g_value_take_object(value, self->priv->parent_window);
            break;

        case PROP_PARENT:
            g_value_take_object(value, self->priv->parent);
            break;

        case PROP_INPUT:
            g_value_set_boolean(value, self->priv->input);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

/* Callbacks */
static void
mud_log_select_clicked_cb(GtkWidget *widget,
                          MudLog *self)
{
    GtkBuilder *builder;
    GError *error = NULL;
    GtkWidget *dialog;
    gint result;

    builder = gtk_builder_new();
    if(gtk_builder_add_from_file(builder, UIDIR "/main.ui", &error) == 0)
        g_error("Failed to load: %s", error->message);

    dialog = GTK_WIDGET(gtk_builder_get_object(builder, "save_dialog"));
    g_object_unref(builder);

    g_object_set(dialog, "title", _("Save log as…"), NULL);

    if(strlen(gtk_entry_get_text(GTK_ENTRY(self->priv->entry_file))) != 0)
    {
        const gchar *text = gtk_entry_get_text(GTK_ENTRY(self->priv->entry_file));
        gchar *dir = g_path_get_dirname(text);
        gchar *file = g_path_get_basename(text);

        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),
                                            dir);
        gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog),
                                           file);

        g_free(dir);
        g_free(file);
    }
    else
    {
        gchar *name = g_strdup_printf("%s.log", self->mud_name);

        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),
                                            g_get_home_dir());
        gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER(dialog),
                                           name);

        g_free(name);
    }

    result = gtk_dialog_run(GTK_DIALOG(dialog));

    if(result == GTK_RESPONSE_OK)
    {
        if(self->priv->filename)
        {
            g_free(self->priv->filename);
            self->priv->filename = NULL;
        }

        self->priv->filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

        gtk_entry_set_text(GTK_ENTRY(self->priv->entry_file), self->priv->filename);
    }

    gtk_widget_destroy(dialog);
}

static void
mud_log_next_toggled_cb(GtkToggleButton *button,
                        MudLog *self)
{
    gboolean active;

    g_object_get(button, "active", &active, NULL);

    self->priv->include_next = active;

    if(self->priv->include_next)
    {
        self->priv->next_count = gtk_spin_button_get_value_as_int(
                GTK_SPIN_BUTTON(self->priv->spin_log_next));

        gtk_widget_set_sensitive(self->priv->spin_log_next, TRUE);
        gtk_widget_grab_focus(self->priv->spin_log_next);
    }
    else
    {
        gtk_widget_set_sensitive(self->priv->spin_log_next, FALSE);
        self->priv->next_count = 0;
    }
}

static void
mud_log_prev_toggled_cb(GtkToggleButton *button,
                        MudLog *self)
{
    gboolean active;

    g_object_get(button, "active", &active, NULL);

    self->priv->include_prev = active;

    if(self->priv->include_prev)
    {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->priv->check_buffer),
                                     FALSE);

        self->priv->prev_count = gtk_spin_button_get_value_as_int(
                GTK_SPIN_BUTTON(self->priv->spin_log_prev));

        gtk_widget_set_sensitive(self->priv->spin_log_prev, TRUE);
        gtk_widget_grab_focus(self->priv->spin_log_prev);
    }
    else
    {
        gtk_widget_set_sensitive(self->priv->spin_log_prev, FALSE);
        self->priv->prev_count = 0;
    }
}


static void
mud_log_append_toggled_cb(GtkToggleButton *button,
                          MudLog *self)
{
    gboolean active;

    g_object_get(button, "active", &active, NULL);

    self->priv->append = active;

    if(self->priv->append)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->priv->check_color),
                                     FALSE);
}

static void
mud_log_input_toggled_cb(GtkToggleButton *button,
                         MudLog *self)
{
    gboolean active;

    g_object_get(button, "active", &active, NULL);

    self->priv->input = active;
}

static void
mud_log_buffer_toggled_cb(GtkToggleButton *button,
                          MudLog *self)
{
    gboolean active;

    g_object_get(button, "active", &active, NULL);

    self->priv->buffer = active;

    if(self->priv->buffer)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->priv->check_log_prev),
                                     FALSE);
}

static void
mud_log_color_toggled_cb(GtkToggleButton *button,
                         MudLog *self)
{
    gboolean active;

    g_object_get(button, "active", &active, NULL);

    self->priv->color = active;

    if(self->priv->color)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->priv->check_append),
                                     FALSE);
}

static gboolean
mud_log_keypress_cb(GtkWidget *widget,
                    GdkEventKey *event,
                    MudLog *self)
{
    if(self->priv->filename)
    {
        g_free(self->priv->filename);
        self->priv->filename = NULL;
    }

    self->priv->filename = g_strdup(
            gtk_entry_get_text(GTK_ENTRY(self->priv->entry_file)));

    return FALSE;
}

static void
mud_log_next_spin_changed_cb(GtkSpinButton *button,
                             MudLog *self)
{
    self->priv->next_count = gtk_spin_button_get_value_as_int(
                                GTK_SPIN_BUTTON(button));
}

static void
mud_log_prev_spin_changed_cb(GtkSpinButton *button,
                             MudLog *self)
{
    self->priv->prev_count = gtk_spin_button_get_value_as_int(
                                GTK_SPIN_BUTTON(button));
}

static void
mud_log_line_added_cb(MudLineBuffer *buffer,
                      MudLineBufferLine *line,
                      MudLog *self)
{
    if(!self->priv->done)
    {
        if(line && line->line && line->line->len != 0)
            mud_log_write(self, line->line->str, line->line->len);

        if(self->priv->include_next)
        {
            self->priv->count++;

            if(self->priv->count == self->priv->next_count)
            {
                self->priv->count = 0;
                self->priv->next_count = 0;

                self->priv->done = TRUE;
            }
        }
    }
}

/* Public Methods */
void
mud_log_open(MudLog *self)
{
    GtkBuilder *builder;
    GError *error = NULL;
    GtkWidget *main_window;
    gchar buf[1024];
    time_t t;
    gint result;

    g_return_if_fail(MUD_IS_LOG(self));

    builder = gtk_builder_new();
    if(gtk_builder_add_from_file(builder, UIDIR "/main.ui", &error) == 0)
        g_error("Failed to load: %s", error->message);

    self->priv->window = GTK_WIDGET(gtk_builder_get_object(builder, "log_config_window"));
    self->priv->check_log_next = GTK_WIDGET(gtk_builder_get_object(builder, "inc_next_check_btn"));
    self->priv->spin_log_next = GTK_WIDGET(gtk_builder_get_object(builder, "next_spin_btn"));
    self->priv->check_log_prev = GTK_WIDGET(gtk_builder_get_object(builder, "inc_prev_check_btn"));
    self->priv->spin_log_prev = GTK_WIDGET(gtk_builder_get_object(builder, "prev_spin_btn"));
    self->priv->check_append = GTK_WIDGET(gtk_builder_get_object(builder, "append_check_btn"));
    self->priv->check_input = GTK_WIDGET(gtk_builder_get_object(builder, "input_check_btn"));
    self->priv->check_buffer = GTK_WIDGET(gtk_builder_get_object(builder, "buffer_check_btn"));
    self->priv->check_color = GTK_WIDGET(gtk_builder_get_object(builder, "color_check_btn"));
    self->priv->btn_select = GTK_WIDGET(gtk_builder_get_object(builder, "select_btn"));
    self->priv->entry_file = GTK_WIDGET(gtk_builder_get_object(builder, "file_entry"));

    g_object_get(self->priv->parent_window, "window", &main_window, NULL);

    gtk_window_set_transient_for(GTK_WINDOW(self->priv->window),
                                 GTK_WINDOW(main_window));

    g_signal_connect(self->priv->spin_log_next,
                     "value-changed",
                     G_CALLBACK(mud_log_next_spin_changed_cb),
                     self);

    g_signal_connect(self->priv->spin_log_prev,
                     "value-changed",
                     G_CALLBACK(mud_log_prev_spin_changed_cb),
                     self);

    g_signal_connect(self->priv->entry_file,
                     "key-press-event",
                     G_CALLBACK(mud_log_keypress_cb),
                     self);

    g_signal_connect(self->priv->btn_select,
                     "clicked",
                     G_CALLBACK(mud_log_select_clicked_cb),
                     self);

    g_signal_connect(self->priv->check_log_next,
                     "toggled",
                     G_CALLBACK(mud_log_next_toggled_cb),
                     self);

    g_signal_connect(self->priv->check_log_prev,
                     "toggled",
                     G_CALLBACK(mud_log_prev_toggled_cb),
                     self);

    g_signal_connect(self->priv->check_append,
                     "toggled",
                     G_CALLBACK(mud_log_append_toggled_cb),
                     self);

    g_signal_connect(self->priv->check_input,
                     "toggled",
                     G_CALLBACK(mud_log_input_toggled_cb),
                     self);

    g_signal_connect(self->priv->check_buffer,
                     "toggled",
                     G_CALLBACK(mud_log_buffer_toggled_cb),
                     self);

    g_signal_connect(self->priv->check_color,
                     "toggled",
                     G_CALLBACK(mud_log_color_toggled_cb),
                     self);

    g_object_unref(builder);

    if(self->priv->filename)
        g_free(self->priv->filename);

    self->priv->filename = g_strdup_printf("%s%c%s.log",
                                          g_get_home_dir(),
                                          G_DIR_SEPARATOR,
                                          self->mud_name);

    gtk_entry_set_text(GTK_ENTRY(self->priv->entry_file),
                       self->priv->filename);

    result = gtk_dialog_run(GTK_DIALOG(self->priv->window));
    
    if(result == GTK_RESPONSE_OK)
    {
        if(self->priv->color)
        {
            gchar *temp = self->priv->filename;

            self->priv->filename = g_strdup_printf("%s.html", self->priv->filename);

            g_free(temp);

        }
        self->priv->logfile = fopen(self->priv->filename,
                                    (self->priv->append) ? "a" : "w");
        if (self->priv->logfile)
        {
            if(self->priv->color)
                mud_log_write_html_header(self);

            time(&t);
            strftime(buf, 1024,
                    _("\n*** Log starts *** %d/%m/%Y %H∶%M∶%S\n"),
                    localtime(&t));
            fprintf(self->priv->logfile, "%s", buf);
            fdatasync(fileno(self->priv->logfile));

            if(self->priv->buffer)
            {
                VteTerminal *term;
                GtkClipboard *clipboard;
                gchar *term_text;

                g_object_get(self->priv->parent, "terminal", &term, NULL);

                clipboard = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
                vte_terminal_select_all(term);
                vte_terminal_copy_primary(term);
                term_text = gtk_clipboard_wait_for_text(clipboard);
                vte_terminal_select_none(term);

                if(term_text)
                {
                    fprintf(self->priv->logfile, "%s", term_text);
                    fdatasync(fileno(self->priv->logfile));
                    g_free(term_text);
                }
            }

            if(self->priv->include_prev)
            {
                VteTerminal *term;
                GtkClipboard *clipboard;
                gchar *term_text;
                gchar *buf_text;

                MudLineBuffer *buffer = g_object_new(MUD_TYPE_LINE_BUFFER,
                                                     "maximum-line-count", self->priv->prev_count,
                                                     NULL);

                g_object_get(self->priv->parent, "terminal", &term, NULL);

                clipboard = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
                vte_terminal_select_all(term);
                vte_terminal_copy_primary(term);
                term_text = gtk_clipboard_wait_for_text(clipboard);
                vte_terminal_select_none(term);

                if(term_text)
                {
                    mud_line_buffer_add_data(buffer, term_text, strlen(term_text));
                    g_free(term_text);

                    buf_text = mud_line_buffer_get_lines(buffer);

                    fprintf(self->priv->logfile, "%s", buf_text);
                    fdatasync(fileno(self->priv->logfile));

                    g_free(buf_text);
                }

                g_object_unref(buffer);
            }

            if(self->priv->include_next)
                self->priv->count = 0;

            self->priv->line_buffer = g_object_new(MUD_TYPE_LINE_BUFFER,
                                                   "maximum-line-count", 5,
                                                   NULL);

            g_signal_connect(self->priv->line_buffer,
                             "line-added",
                             G_CALLBACK(mud_log_line_added_cb),
                             self);


            self->priv->active = TRUE;
        }
        else
        {
            GtkWidget *main_window;

            g_object_get(self->priv->window, "window", &main_window, NULL);

            utils_error_message(main_window,
                                _("Log Error"),
                                _("Could not open “%s” for writing."),
                                self->priv->filename);
        }
    }

    gtk_widget_destroy(self->priv->window);
}

void
mud_log_close(MudLog *log)
{
    gchar buf[255];
    time_t t;

    g_return_if_fail(MUD_IS_LOG(log));

    if(log->priv->color)
    {
        gsize write_size;
        const gchar span[] = "</span>";

        while(!g_queue_is_empty(log->priv->span_queue))
        {
            write_size = fwrite(span, 1, sizeof(span) - 1,
                                log->priv->logfile);
            if(write_size != sizeof(span) - 1)
                g_critical(_("Could not write data to log file!"));
            g_queue_pop_head(log->priv->span_queue);
        }
    }

    time(&t);
    strftime(buf, 255,
            _("\n *** Log stops *** %d/%m/%Y %H∶%M∶%S\n"),
            localtime(&t));

    fprintf(log->priv->logfile, "%s", buf);

    if(log->priv->color)
        mud_log_write_html_footer(log);

    fdatasync(fileno(log->priv->logfile));
    fclose(log->priv->logfile);

    if(log->priv->filename)
    {
        g_free(log->priv->filename);
        log->priv->filename = NULL;
    }

    g_object_unref(log->priv->line_buffer);
    log->priv->active = FALSE;

    if(!log->priv->finalizing)
    {
        g_object_set(log->priv->parent, "logging", FALSE, NULL);
        mud_window_update_logging_ui(log->priv->parent_window,
                log->priv->parent,
                FALSE);
    }
}

gboolean
mud_log_islogging(MudLog *log)
{
    if(!log)
        return FALSE;

    return log->priv->active;
}

void
mud_log_write_hook(MudLog *log, const gchar *data, gint length)
{
    g_return_if_fail(MUD_IS_LOG(log));

    if(log->priv->done)
    {
        log->priv->done = FALSE;
        mud_log_close(log);
    }
    else if(log->priv->active)
    {
        mud_line_buffer_add_data(log->priv->line_buffer, data, length);
    }
}

/* Private Methods */
static void
mud_log_write(MudLog *log, const gchar *data, gsize size)
{
    gchar *stripData;
    gint stripSize = 0;
    gsize write_size;

    g_return_if_fail(MUD_IS_LOG(log));
    g_return_if_fail(log->priv->logfile != NULL);
    g_return_if_fail(data != NULL);

    if(!log->priv->color)
    {
        stripData = utils_strip_ansi(data);
        stripSize = strlen(stripData);

        write_size = fwrite(stripData, 1, stripSize, log->priv->logfile);

        if(write_size != stripSize)
            g_critical(_("Could not write data to log file!"));

        g_free(stripData);
    }
    else
    {
        gchar *output;

        output = mud_log_parse_ansi(log, data, size);

        if(output)
        {
            const gsize output_len = strlen(output);

            write_size = fwrite(output, 1, output_len, log->priv->logfile);
            if(write_size != output_len)
                g_critical(_("Could not write data to log file!"));

            g_free(output);
        }
    }
}

static gchar * 
mud_log_parse_ansi(MudLog *self,
                   const gchar *data,
                   gsize size)
{
    gsize i;
    GString *ecma_color;
    GString *output;

    output = g_string_new(NULL);

    for(i = 0; i < size; ++i)
    {
        if(data[i] == ECMA_ESCAPE_BYTE_C)
        {
            ecma_color = g_string_new(NULL);
            ++i;

            while(data[++i] != 'm' && i < size)
                ecma_color = g_string_append_c(ecma_color, data[i]);

            mud_log_parse_ecma_color(self,
                                     ecma_color->str,
                                     ecma_color->len,
                                     output);

            g_string_free(ecma_color, TRUE);

            continue;
        }

        if(data[i] != '\r') // Just log newlines
        {
            if(data[i] == '<')
                output = g_string_append(output, "&lt;");
            else if(data[i] == '>')
                output = g_string_append(output, "&gt;");
            else
                output = g_string_append_c(output, data[i]);
        }
    }

    return g_string_free(output, (output->len == 0) );
}

#define STATE_ATTRIBUTE 0
#define STATE_FORECOLOR 1
#define STATE_BACKCOLOR 2

static void
mud_log_parse_ecma_color(MudLog *self,
                         const gchar *data,
                         gsize size,
                         GString *output)
{
    gint i, argc, byte;
    gchar **argv;
    gboolean xterm_forecolor, xterm_color;

    g_printf("%s\n", data);
    argv = g_strsplit(data, ";", -1);
    argc = g_strv_length(argv);

    xterm_forecolor = FALSE;
    xterm_color = FALSE;

    if(argc < 1 || argc > 3)
    {
        g_strfreev(argv);

        return;
    }

    for(i = 0; i < argc; ++i)
    {
        switch(i)
        {
            case STATE_ATTRIBUTE:
                byte = (gint)atol(argv[0]);

                switch(byte)
                {
                    case ECMA_ATTRIBUTE_NORMAL:
                        while(!g_queue_is_empty(self->priv->span_queue))
                        {
                            output = g_string_append(output, "</span>");
                            g_queue_pop_head(self->priv->span_queue);
                        }

                        self->priv->bold = FALSE;
                        break;

                    case ECMA_ATTRIBUTE_BOLD:
                        output = g_string_append(output, "<span style=\"font-weight: bold;\">");
                        g_queue_push_head(self->priv->span_queue, GINT_TO_POINTER(ECMA_ATTRIBUTE_BOLD));
                        self->priv->bold = TRUE;
                        break;

                    case ECMA_ATTRIBUTE_ITALIC:
                        output = g_string_append(output, "<span style=\"font-style: italic;\">");
                        g_queue_push_head(self->priv->span_queue, GINT_TO_POINTER(ECMA_ATTRIBUTE_ITALIC));
                        break;

                    case ECMA_ATTRIBUTE_UNDERLINE:
                        output = g_string_append(output, "<span style=\"text-decoration: underline;\">");
                        g_queue_push_head(self->priv->span_queue, GINT_TO_POINTER(ECMA_ATTRIBUTE_UNDERLINE));
                        break;

                    case ECMA_ATTRIBUTE_BLINK:
                        output = g_string_append(output, "<span style=\"text-decoration: blink;\">");
                        g_queue_push_head(self->priv->span_queue, GINT_TO_POINTER(ECMA_ATTRIBUTE_BLINK));
                        break;

                    /* FIXME: Figure out what to do to make text reversed in the browser */
                    case ECMA_ATTRIBUTE_REVERSE:
                        output = g_string_append(output, "<span>");
                        g_queue_push_head(self->priv->span_queue, GINT_TO_POINTER(ECMA_ATTRIBUTE_REVERSE));
                        break;

                    case ECMA_ATTRIBUTE_NODISPLAY:
                        // Dont' display it.
                        break;

                        /* Skip forecolor state for xterm colors,
                         * always 5. */
                    case XTERM_FORECOLOR:
                        i = 1;
                        xterm_forecolor = TRUE;
                        xterm_color = TRUE;
                        break;

                    case XTERM_BACKCOLOR:
                        i = 1;
                        xterm_forecolor = FALSE;
                        xterm_color = TRUE;
                        break;

                    case ECMA_FORECOLOR_BLACK:
                        mud_log_write_html_foreground_span(self,
                                output,
                                self->priv->bold,
                                ECMA_FORECOLOR_BLACK);
                        break;

                    case ECMA_FORECOLOR_RED:
                        mud_log_write_html_foreground_span(self,
                                output,
                                self->priv->bold,
                                ECMA_FORECOLOR_RED);
                        break;

                    case ECMA_FORECOLOR_GREEN:
                        mud_log_write_html_foreground_span(self,
                                output,
                                self->priv->bold,
                                ECMA_FORECOLOR_GREEN);
                        break;

                    case ECMA_FORECOLOR_YELLOW:
                        mud_log_write_html_foreground_span(self,
                                output,
                                self->priv->bold,
                                ECMA_FORECOLOR_YELLOW);
                        break;

                    case ECMA_FORECOLOR_BLUE:
                        mud_log_write_html_foreground_span(self,
                                output,
                                self->priv->bold,
                                ECMA_FORECOLOR_BLUE);
                        break;

                    case ECMA_FORECOLOR_MAGENTA:
                        mud_log_write_html_foreground_span(self,
                                output,
                                self->priv->bold,
                                ECMA_FORECOLOR_MAGENTA);
                        break;

                    case ECMA_FORECOLOR_CYAN:
                        mud_log_write_html_foreground_span(self,
                                output,
                                self->priv->bold,
                                ECMA_FORECOLOR_CYAN);
                        break;

                    case ECMA_FORECOLOR_WHITE:
                        mud_log_write_html_foreground_span(self,
                                output,
                                self->priv->bold,
                                ECMA_FORECOLOR_WHITE);

                    case ECMA_BACKCOLOR_BLACK:
                        mud_log_write_html_background_span(self,
                                output,
                                FALSE,
                                ECMA_BACKCOLOR_BLACK);
                        break;

                    case ECMA_BACKCOLOR_RED:
                        mud_log_write_html_background_span(self,
                                output,
                                FALSE,
                                ECMA_BACKCOLOR_RED);
                        break;

                    case ECMA_BACKCOLOR_GREEN:
                        mud_log_write_html_background_span(self,
                                output,
                                FALSE,
                                ECMA_BACKCOLOR_GREEN);
                        break;

                    case ECMA_BACKCOLOR_YELLOW:
                        mud_log_write_html_background_span(self,
                                output,
                                FALSE,
                                ECMA_BACKCOLOR_YELLOW);
                        break;

                    case ECMA_BACKCOLOR_BLUE:
                        mud_log_write_html_background_span(self,
                                output,
                                FALSE,
                                ECMA_BACKCOLOR_BLUE);
                        break;

                    case ECMA_BACKCOLOR_MAGENTA:
                        mud_log_write_html_background_span(self,
                                output,
                                FALSE,
                                ECMA_BACKCOLOR_MAGENTA);
                        break;

                    case ECMA_BACKCOLOR_CYAN:
                        mud_log_write_html_background_span(self,
                                output,
                                FALSE,
                                ECMA_BACKCOLOR_CYAN);
                        break;

                    case ECMA_BACKCOLOR_WHITE:
                        mud_log_write_html_background_span(self,
                                output,
                                FALSE,
                                ECMA_BACKCOLOR_WHITE);
                        break;

                }
                break;

            case STATE_FORECOLOR:
                byte = (gint)atol(argv[1]);

                switch(byte)
                {
                    case ECMA_FORECOLOR_BLACK:
                        mud_log_write_html_foreground_span(self,
                                                           output,
                                                           self->priv->bold,
                                                           ECMA_FORECOLOR_BLACK);
                        break;

                    case ECMA_FORECOLOR_RED:
                        mud_log_write_html_foreground_span(self,
                                                           output,
                                                           self->priv->bold,
                                                           ECMA_FORECOLOR_RED);
                        break;

                    case ECMA_FORECOLOR_GREEN:
                        mud_log_write_html_foreground_span(self,
                                                           output,
                                                           self->priv->bold,
                                                           ECMA_FORECOLOR_GREEN);
                        break;

                    case ECMA_FORECOLOR_YELLOW:
                        mud_log_write_html_foreground_span(self,
                                                           output,
                                                           self->priv->bold,
                                                           ECMA_FORECOLOR_YELLOW);
                        break;

                    case ECMA_FORECOLOR_BLUE:
                        mud_log_write_html_foreground_span(self,
                                                           output,
                                                           self->priv->bold,
                                                           ECMA_FORECOLOR_BLUE);
                        break;

                    case ECMA_FORECOLOR_MAGENTA:
                        mud_log_write_html_foreground_span(self,
                                                           output,
                                                           self->priv->bold,
                                                           ECMA_FORECOLOR_MAGENTA);
                        break;

                    case ECMA_FORECOLOR_CYAN:
                        mud_log_write_html_foreground_span(self,
                                                           output,
                                                           self->priv->bold,
                                                           ECMA_FORECOLOR_CYAN);
                        break;

                    case ECMA_FORECOLOR_WHITE:
                        mud_log_write_html_foreground_span(self,
                                                           output,
                                                           self->priv->bold,
                                                           ECMA_FORECOLOR_WHITE);
                }
                break;

            case STATE_BACKCOLOR:
                byte = (gint)atol(argv[2]);

                if(xterm_color)
                {
                    mud_log_write_html_xterm_span(self,
                            output,
                            xterm_forecolor,
                            byte);

                    xterm_color = FALSE;
                }
                else
                {
                    switch(byte)
                    {
                        case ECMA_BACKCOLOR_BLACK:
                            mud_log_write_html_background_span(self,
                                                               output,
                                                               FALSE,
                                                               ECMA_BACKCOLOR_BLACK);
                            break;

                        case ECMA_BACKCOLOR_RED:
                            mud_log_write_html_background_span(self,
                                                               output,
                                                               FALSE,
                                                               ECMA_BACKCOLOR_RED);
                            break;

                        case ECMA_BACKCOLOR_GREEN:
                            mud_log_write_html_background_span(self,
                                                               output,
                                                               FALSE,
                                                               ECMA_BACKCOLOR_GREEN);
                            break;

                        case ECMA_BACKCOLOR_YELLOW:
                            mud_log_write_html_background_span(self,
                                                               output,
                                                               FALSE,
                                                               ECMA_BACKCOLOR_YELLOW);
                            break;

                        case ECMA_BACKCOLOR_BLUE:
                            mud_log_write_html_background_span(self,
                                                               output,
                                                               FALSE,
                                                               ECMA_BACKCOLOR_BLUE);
                            break;

                        case ECMA_BACKCOLOR_MAGENTA:
                            mud_log_write_html_background_span(self,
                                                               output,
                                                               FALSE,
                                                               ECMA_BACKCOLOR_MAGENTA);
                            break;

                        case ECMA_BACKCOLOR_CYAN:
                            mud_log_write_html_background_span(self,
                                                               output,
                                                               FALSE,
                                                               ECMA_BACKCOLOR_CYAN);
                            break;

                        case ECMA_BACKCOLOR_WHITE:
                            mud_log_write_html_background_span(self,
                                                               output,
                                                               FALSE,
                                                               ECMA_BACKCOLOR_WHITE);
                            break;


                    }
                }
                break;
        }
    }

    g_strfreev(argv);
}

static void
mud_log_write_html_header(MudLog *self)
{
    gchar *title = g_path_get_basename(self->priv->filename);

    fprintf(self->priv->logfile, "%s", "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(self->priv->logfile, "%s", "\t<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\"\n ");
    fprintf(self->priv->logfile, "%s", "\t\t\"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">\n\n");

    fprintf(self->priv->logfile, "%s", "<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"en\" lang=\"en\">\n");

    fprintf(self->priv->logfile, "\t<head>\n\t\t<title>%s</title>\n\n\t\t",
            title);

    g_free(title);

    fprintf(self->priv->logfile, "%s", "<style type=\"text/css\">\n\t\t\t");

    fprintf(self->priv->logfile, "body {\n\t\t\t\tbackground-color: rgb(%d, %d, %d);\n",
                                 self->priv->parent->profile->preferences->Background.red / 256,
                                 self->priv->parent->profile->preferences->Background.green / 256,
                                 self->priv->parent->profile->preferences->Background.blue / 256);

    fprintf(self->priv->logfile, "\t\t\t\tcolor: rgb(%d, %d, %d);\n",
                                 self->priv->parent->profile->preferences->Foreground.red / 256,
                                 self->priv->parent->profile->preferences->Foreground.green / 256,
                                 self->priv->parent->profile->preferences->Foreground.blue / 256);

    fprintf(self->priv->logfile, "%s", "\t\t\t\tfont-family: monospace;\n");

    fprintf(self->priv->logfile, "\t\t\t}\n");

    fprintf(self->priv->logfile, "%s", "\t\t</style>\n\n\t</head>\n\n\t<body>\n\t\t<pre>\n");

    fsync(fileno(self->priv->logfile));
}

static void
mud_log_write_html_footer(MudLog *self)
{
    fprintf(self->priv->logfile, "%s", "\t\t</pre>\n\t</body>\n</html>\n");
    fprintf(self->priv->logfile, "<!-- Generated by Gnome-Mud %s http://live.gnome.org/GnomeMud -->\n", VERSION);

    fsync(fileno(self->priv->logfile));
}

static void
mud_log_write_html_foreground_span(MudLog *self,
                                   GString *output,
                                   gboolean bold,
                                   gint ecma_code)
{
    gint color_index = ecma_code - 30;

    /* Work around some gnome-mud palette weirdness */
    switch(ecma_code)
    {
        case ECMA_FORECOLOR_GREEN:
            color_index = 4;
            break;

        case ECMA_FORECOLOR_YELLOW:
            color_index = 5;
            break;

        case ECMA_FORECOLOR_BLUE:
            color_index = 2; 
            break;

        case ECMA_FORECOLOR_MAGENTA:
            color_index = 3;
            break;
    }

    color_index += (bold) ? 8 : 0;

    g_string_append_printf(output, "<span style=\"color: rgb(%d, %d, %d);\">",
            self->priv->parent->profile->preferences->Colors[color_index].red / 256,
            self->priv->parent->profile->preferences->Colors[color_index].blue / 256,
            self->priv->parent->profile->preferences->Colors[color_index].green / 256);
    g_queue_push_head(self->priv->span_queue, GINT_TO_POINTER(ecma_code));
}

static void
mud_log_write_html_background_span(MudLog *self,
                                   GString *output,
                                   gboolean bold,
                                   gint ecma_code)
{
    gint color_index = ecma_code - 40;

    /* Work around some gnome-mud palette weirdness */
    switch(ecma_code)
    {
        case ECMA_BACKCOLOR_GREEN:
            color_index = 4;
            break;

        case ECMA_BACKCOLOR_YELLOW:
            color_index = 5;
            break;

        case ECMA_BACKCOLOR_BLUE:
            color_index = 2; 
            break;

        case ECMA_BACKCOLOR_MAGENTA:
            color_index = 3;
            break;
    }

    color_index += (bold) ? 8 : 0;

    g_string_append_printf(output, "<span style=\"background-color: rgb(%d, %d, %d);\">",
            self->priv->parent->profile->preferences->Colors[color_index].red / 256,
            self->priv->parent->profile->preferences->Colors[color_index].blue / 256,
            self->priv->parent->profile->preferences->Colors[color_index].green / 256);
    g_queue_push_head(self->priv->span_queue, GINT_TO_POINTER(ecma_code));
}

static void
mud_log_write_html_xterm_span(MudLog *self,
                              GString *output,
                              gboolean foreground,
                              gint color)
{
    gchar *css_color;

    if(color > 255)
        return;

    if(color < 16) // Drop back down to the standard ECMA-48 colors.
    {
        gboolean bolded = (color > 7);

        if(bolded)
            color -= 8;

        if(foreground)
            mud_log_write_html_foreground_span(self,
                                               output,
                                               bolded,
                                               color + 30);
        else
            mud_log_write_html_background_span(self,
                                               output,
                                               bolded, // xterm can have bolded backgrounds.
                                               color + 40);

        return;
    }

    if(foreground)
        css_color = g_strdup("color: ");
    else
        css_color = g_strdup("background-color: ");

    g_string_append_printf(output, "<span style=\"%s rgb(%d, %d, %d);\">",
            css_color,
            self->priv->xterm_colors[color].red / 256,
            self->priv->xterm_colors[color].blue / 256,
            self->priv->xterm_colors[color].green / 256);
    g_queue_push_head(self->priv->span_queue,
                      (foreground) ?
                        GINT_TO_POINTER(XTERM_FORECOLOR) :
                        GINT_TO_POINTER(XTERM_BACKCOLOR)
                        );

    g_free(css_color);
}

static void
mud_log_create_xterm_colors(MudLog *self)
{
    gint i;
    GString *color_string;

    /* Pulled from libvte */
    for(i = 16; i < 232; ++i)
    {
        gint j = i - 16;
        gint r = j / 36, g = (j / 6) % 6, b = j % 6;
        gint red =   (r == 0) ? 0 : r * 40 + 55;
        gint green = (g == 0) ? 0 : g * 40 + 55;
        gint blue =  (b == 0) ? 0 : b * 40 + 55;
        self->priv->xterm_colors[i].red   = red | red << 8  ;
        self->priv->xterm_colors[i].green = green | green << 8;
        self->priv->xterm_colors[i].blue  = blue | blue << 8;
    }

    /* Generate Grays */
    for(i = 0; i < 24; i++)
    {
        gint gray = i * 10 + 8;
        color_string = g_string_new(NULL);
        g_string_printf(color_string,
                        "#%2.2x%2.2x%2.2x",
                        gray, gray, gray);

        gdk_color_parse(color_string->str,
                        &self->priv->xterm_colors[232 + i]);

        g_string_free(color_string, TRUE);
    }
}


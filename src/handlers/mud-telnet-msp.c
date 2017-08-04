/* GNOME-Mud - A simple Mud Client
 * mud-telnet-msp.c
 * Copyright (C) 2008-2009 Les Harris <lharris@gnome.org>
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

#ifdef ENABLE_GST

#include <glib.h>
#include <string.h>
#include <gst/gst.h>
#include <ctype.h>
#include <glib/gprintf.h>

#include "gnome-mud.h"
#include "mud-telnet.h"
#include "mud-telnet-handler-interface.h"
#include "mud-telnet-msp.h"
#include "mud-line-buffer.h"

struct _MudTelnetMspPrivate
{
    /* Interface Properties */
    MudTelnet *telnet;
    gboolean enabled;
    gint option;

    /* Private Instance Members */
    MudMSPParser msp_parser;
    MudMSPTypes msp_type;
    MudMSPSound sound[2];
    gchar *base_url;
};

/* Property Identifiers */
enum
{
    PROP_MUD_TELNET_MSP_0,
    PROP_ENABLED,
    PROP_HANDLES_OPTION,
    PROP_TELNET
};

/* Class Functions */
static void mud_telnet_msp_init (MudTelnetMsp *self);
static void mud_telnet_msp_class_init (MudTelnetMspClass *klass);
static void mud_telnet_msp_interface_init(MudTelnetHandlerInterface *iface);
static void mud_telnet_msp_finalize (GObject *object);
static GObject *mud_telnet_msp_constructor (GType gtype,
                                             guint n_properties,
                                             GObjectConstructParam *properties);
static void mud_telnet_msp_set_property(GObject *object,
                                         guint prop_id,
                                         const GValue *value,
                                         GParamSpec *pspec);
static void mud_telnet_msp_get_property(GObject *object,
                                         guint prop_id,
                                         GValue *value,
                                         GParamSpec *pspec);

/*Interface Implementation */
void mud_telnet_msp_enable(MudTelnetHandler *self);
void mud_telnet_msp_disable(MudTelnetHandler *self);
void mud_telnet_msp_handle_sub_neg(MudTelnetHandler *self,
                                    guchar *buf,
                                    guint len);


/* Create the Type. We implement MudTelnetHandlerInterface */
G_DEFINE_TYPE_WITH_CODE(MudTelnetMsp, mud_telnet_msp, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE (MUD_TELNET_HANDLER_TYPE,
                                               mud_telnet_msp_interface_init));

/* Private Methods */
static void mud_telnet_msp_parser_reset(MudTelnetMsp *self);
static void mud_telnet_msp_parser_args(MudTelnetMsp *self);
static void mud_telnet_msp_command_free(MudMSPCommand *command);
static gboolean mud_telnet_msp_parser_is_param_char(gchar c);
static gboolean mud_telnet_msp_parser_switch_on_param_char(gint *state,
        gchar *buf,
        gint index,
        gint len);
static void mud_telnet_msp_process_command(MudTelnetMsp *self,
                                           MudMSPCommand *command);
static void mud_telnet_msp_start_playing(MudTelnetMsp *self, MudMSPTypes type);
static gboolean mud_telnet_msp_get_files(MudTelnetMsp *self, MudMSPTypes type);
static gboolean mud_telnet_msp_sound_bus_call (GstBus *bus,
                                               GstMessage *msg,
                                               gpointer data);
static gboolean mud_telnet_msp_music_bus_call (GstBus *bus,
                                               GstMessage *msg,
                                               gpointer data);

/* MudTelnetMsp class functions */
static void
mud_telnet_msp_class_init (MudTelnetMspClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    /* Override base object constructor */
    object_class->constructor = mud_telnet_msp_constructor;

    /* Override base object's finalize */
    object_class->finalize = mud_telnet_msp_finalize;

    /* Override base object property methods */
    object_class->set_property = mud_telnet_msp_set_property;
    object_class->get_property = mud_telnet_msp_get_property;

    /* Add private data to class */
    g_type_class_add_private(klass, sizeof(MudTelnetMspPrivate));

    /* Override Implementation Properties */
    g_object_class_override_property(object_class,
                                     PROP_ENABLED,
                                     "enabled");

    g_object_class_override_property(object_class,
                                     PROP_HANDLES_OPTION,
                                     "handles-option");

    g_object_class_override_property(object_class,
                                     PROP_TELNET,
                                     "telnet");
}

static void
mud_telnet_msp_interface_init(MudTelnetHandlerInterface *iface)
{
    iface->Enable = mud_telnet_msp_enable;
    iface->Disable = mud_telnet_msp_disable;
    iface->HandleSubNeg = mud_telnet_msp_handle_sub_neg;
}

static void
mud_telnet_msp_init (MudTelnetMsp *self)
{
    /* Get our private data */
    self->priv = MUD_TELNET_MSP_GET_PRIVATE(self);

    /* Set the defaults */
    self->priv->telnet = NULL;
    self->priv->option = TELOPT_MSP;
    self->priv->enabled = FALSE;

    self->priv->sound[0].files = NULL;
    self->priv->sound[0].current_command = NULL;
    self->priv->sound[0].playing = FALSE;
    self->priv->sound[0].files_len = 0;

    self->priv->sound[1].files = NULL;
    self->priv->sound[1].current_command = NULL;
    self->priv->sound[1].playing = FALSE;
    self->priv->sound[1].files_len = 0;

    self->priv->base_url = NULL;
}

static GObject *
mud_telnet_msp_constructor (GType gtype,
                            guint n_properties,
                            GObjectConstructParam *properties)
{
    MudTelnetMsp *self;
    GObject *obj;
    MudTelnetMspClass *klass;
    GObjectClass *parent_class;

    /* Chain up to parent constructor */
    klass = MUD_TELNET_MSP_CLASS( g_type_class_peek(MUD_TYPE_TELNET_MSP) );
    parent_class = G_OBJECT_CLASS( g_type_class_peek_parent(klass) );
    obj = parent_class->constructor(gtype, n_properties, properties);

    self = MUD_TELNET_MSP(obj);

    if(!self->priv->telnet)
    {
        g_printf("ERROR: Tried to instantiate MudTelnetMsp without passing parent MudTelnet\n");
        g_error("Tried to instantiate MudTelnetMsp without passing parent MudTelnet");
    }

    self->priv->msp_parser.enabled = FALSE;
    self->priv->msp_parser.state = MSP_STATE_TEXT;
    self->priv->msp_parser.lex_pos_start = 0;
    self->priv->msp_parser.lex_pos_end = 0;
    self->priv->msp_parser.output = g_string_new(NULL);
    self->priv->msp_parser.arg_buffer = g_string_new(NULL);

    return obj;
}

static void
mud_telnet_msp_finalize (GObject *object)
{
    MudTelnetMsp *self;
    GObjectClass *parent_class;

    self = MUD_TELNET_MSP(object);

    mud_telnet_msp_stop_playing(self, MSP_TYPE_SOUND);
    mud_telnet_msp_stop_playing(self, MSP_TYPE_MUSIC);

    if(self->priv->base_url)
        g_free(self->priv->base_url);
    g_string_free(self->priv->msp_parser.output, TRUE);
    g_string_free(self->priv->msp_parser.arg_buffer, TRUE);

    parent_class = g_type_class_peek_parent(G_OBJECT_GET_CLASS(object));
    parent_class->finalize(object);
}

static void
mud_telnet_msp_set_property(GObject *object,
                            guint prop_id,
                            const GValue *value,
                            GParamSpec *pspec)
{
    MudTelnetMsp *self;

    self = MUD_TELNET_MSP(object);

    switch(prop_id)
    {
        case PROP_TELNET:
            self->priv->telnet = MUD_TELNET(g_value_get_object(value));
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void
mud_telnet_msp_get_property(GObject *object,
                            guint prop_id,
                            GValue *value,
                            GParamSpec *pspec)
{
    MudTelnetMsp *self;

    self = MUD_TELNET_MSP(object);

    switch(prop_id)
    {
        case PROP_ENABLED:
            g_value_set_boolean(value, self->priv->enabled);
            break;

        case PROP_HANDLES_OPTION:
            g_value_set_int(value, self->priv->option);
            break;

        case PROP_TELNET:
            g_value_take_object(value, self->priv->telnet);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

/* Interface Implementation */
void 
mud_telnet_msp_enable(MudTelnetHandler *handler)
{
    MudTelnetMsp *self;

    self = MUD_TELNET_MSP(handler);

    g_return_if_fail(MUD_IS_TELNET_MSP(self));

    self->priv->enabled = TRUE;

    self->priv->msp_parser.enabled = TRUE;
    self->priv->msp_parser.state = MSP_STATE_TEXT;
    self->priv->msp_parser.lex_pos_start = 0;
    self->priv->msp_parser.lex_pos_end = 0;

    g_log("Telnet", G_LOG_LEVEL_INFO, "%s", "MSP Enabled");
}

void
mud_telnet_msp_disable(MudTelnetHandler *handler)
{
    MudTelnetMsp *self;

    self = MUD_TELNET_MSP(handler);

    g_return_if_fail(MUD_IS_TELNET_MSP(self));

    self->priv->enabled = FALSE;

    mud_telnet_msp_stop_playing(self, MSP_TYPE_SOUND);
    mud_telnet_msp_stop_playing(self, MSP_TYPE_MUSIC);

    if(self->priv->base_url)
        g_free(self->priv->base_url);
    g_string_truncate(self->priv->msp_parser.output, 0);
    g_string_truncate(self->priv->msp_parser.arg_buffer, 0);

    self->priv->msp_parser.enabled = FALSE;

    g_log("Telnet", G_LOG_LEVEL_INFO, "%s", "MSP Disabled");
}

void
mud_telnet_msp_handle_sub_neg(MudTelnetHandler *handler,
                              guchar *buf,
                              guint len)
{
    MudTelnetMsp *self;

    self = MUD_TELNET_MSP(handler);

    g_return_if_fail(MUD_IS_TELNET_MSP(self));

    /* There is no MSP subreq.*/
    return;
}

/* Public Methods */
void
mud_telnet_msp_parser_clear(MudTelnetMsp *self)
{
    g_return_if_fail(MUD_IS_TELNET_MSP(self));

    g_string_truncate(self->priv->msp_parser.output, 0);

    self->priv->msp_parser.lex_pos_start = 0;
    self->priv->msp_parser.lex_pos_end = 0;
}

void
mud_telnet_msp_parse(MudTelnetMsp *self, MudLineBufferLine *line)
{
    gsize len = line->line->len;
    gchar *buf = line->line->str;

    g_return_if_fail(MUD_IS_TELNET_MSP(self));

    mud_telnet_msp_parser_reset(self);

    while(self->priv->msp_parser.lex_pos_start < len)
    {
        switch(self->priv->msp_parser.state)
        {
            case MSP_STATE_TEXT:
                if(buf[self->priv->msp_parser.lex_pos_start] == '!')
                    self->priv->msp_parser.state = MSP_STATE_POSSIBLE_COMMAND;
                else
                    self->priv->msp_parser.lex_pos_start++;
                break;

            case MSP_STATE_POSSIBLE_COMMAND:
                if(buf[self->priv->msp_parser.lex_pos_start + 1] != '!')
                    return;

                self->priv->msp_parser.state = MSP_STATE_COMMAND;
                break;

            case MSP_STATE_COMMAND:
                if(buf[self->priv->msp_parser.lex_pos_start + 2] == 'S' &&
                        buf[self->priv->msp_parser.lex_pos_start + 3] == 'O' &&
                        buf[self->priv->msp_parser.lex_pos_start + 4] == 'U' &&
                        buf[self->priv->msp_parser.lex_pos_start + 5] == 'N' &&
                        buf[self->priv->msp_parser.lex_pos_start + 6] == 'D')
                    self->priv->msp_type = MSP_TYPE_SOUND;
                else if(buf[self->priv->msp_parser.lex_pos_start + 2] == 'M' &&
                        buf[self->priv->msp_parser.lex_pos_start + 3] == 'U' &&
                        buf[self->priv->msp_parser.lex_pos_start + 4] == 'S' &&
                        buf[self->priv->msp_parser.lex_pos_start + 5] == 'I' &&
                        buf[self->priv->msp_parser.lex_pos_start + 6] == 'C')
                    self->priv->msp_type = MSP_TYPE_MUSIC;
                else
                {
                    /* Not an msp command, bail out. */
                    return;
                }

                // Skip leading (
                self->priv->msp_parser.lex_pos_start += 8;
                self->priv->msp_parser.state = MSP_STATE_GET_ARGS;
                continue;
                break;

            case MSP_STATE_GET_ARGS:
                self->priv->msp_parser.lex_pos_end =
                    self->priv->msp_parser.lex_pos_start;

                while(self->priv->msp_parser.lex_pos_end < len &&
                        buf[self->priv->msp_parser.lex_pos_end] != ')')
                    self->priv->msp_parser.arg_buffer = 
                        g_string_append_c(self->priv->msp_parser.arg_buffer,
                                buf[self->priv->msp_parser.lex_pos_end++]);

                if(self->priv->msp_parser.lex_pos_end >= len &&
                        buf[self->priv->msp_parser.lex_pos_end - 1] != ')')
                {
                    self->priv->msp_parser.lex_pos_start =
                        self->priv->msp_parser.lex_pos_end;
                    continue;
                }

                self->priv->msp_parser.state = MSP_STATE_PARSE_ARGS;

                break;

            case MSP_STATE_PARSE_ARGS:
                mud_telnet_msp_parser_args(self);

                g_string_truncate(self->priv->msp_parser.arg_buffer, 0);
                self->priv->msp_parser.lex_pos_start =
                    self->priv->msp_parser.lex_pos_end + 1;
                self->priv->msp_parser.state = MSP_STATE_TEXT;

                line->gag = TRUE;

                return;
        }
    }
}

void
mud_telnet_msp_download_item_free(MudMSPDownloadItem *item)
{
    if(!item)
        return;

    if(item->url)
        g_free(item->url);

    if(item->file)
        g_free(item->file);

    g_free(item);
}

void
mud_telnet_msp_stop_playing(MudTelnetMsp *self, MudMSPTypes type)
{
    g_return_if_fail(MUD_IS_TELNET_MSP(self));

    self->priv->sound[type].playing = FALSE;

    if(GST_IS_ELEMENT(self->priv->sound[type].play))
    {
        gst_element_set_state (self->priv->sound[type].play, GST_STATE_NULL);
        gst_object_unref (GST_OBJECT (self->priv->sound[type].play));
    }

    if(self->priv->sound[type].files)
    {
        g_strfreev(self->priv->sound[type].files);
        self->priv->sound[type].files = NULL;
    }

    self->priv->sound[type].files_len = 0;

    mud_telnet_msp_command_free(self->priv->sound[type].current_command);
    self->priv->sound[type].current_command = NULL;
}

/* Private Methods */
static void
mud_telnet_msp_parser_reset(MudTelnetMsp *self)
{
    g_return_if_fail(MUD_IS_TELNET_MSP(self));

    self->priv->msp_parser.lex_pos_start = 0;
    self->priv->msp_parser.lex_pos_end = 0;
}

static void
mud_telnet_msp_parser_args(MudTelnetMsp *self)
{
    gint state = ARG_STATE_FILE;
    gint i;
    GString *buffer;
    gchar *args;
    gint len;
    MudMSPCommand *command;
    MudConnectionView *view;

    g_return_if_fail(MUD_IS_TELNET_MSP(self));

    buffer = g_string_new(NULL);
    args =  g_strdup(self->priv->msp_parser.arg_buffer->str);
    len = strlen(args);;

    command = g_new0(MudMSPCommand, 1);

    g_object_get(self->priv->telnet, "parent-view", &view, NULL);

    command->type = self->priv->msp_type;
    command->fName = NULL;
    command->V = NULL;
    command->L = NULL;
    command->C = NULL;
    command->T = NULL;
    command->U = NULL;
    command->P = NULL;

    g_object_get(view, "mud-name", &command->mud_name, NULL);
    command->sfx_type = NULL;

    /* Load defaults */
    command->volume = 100;
    command->priority = 50;
    command->initial_repeat_count = 1;
    command->current_repeat_count = 1;
    command->loop = FALSE;
    command->cont = (self->priv->msp_type == MSP_TYPE_MUSIC);

    for(i = 0; i < len; ++i)
    {
        if(args[i] == ' ' || args[i] == '=' || args[i] == '"')
            continue;

        switch(state)
        {
            case ARG_STATE_FILE:
                if(mud_telnet_msp_parser_is_param_char(args[i]) &&
                        mud_telnet_msp_parser_switch_on_param_char(&state, args, i, len))
                {
                    command->fName = g_strdup(buffer->str);
                    g_string_free(buffer, TRUE);
                    buffer = g_string_new(NULL);
                }
                else
                    buffer = g_string_append_c(buffer, args[i]);
                break;

            case ARG_STATE_V:
                if(mud_telnet_msp_parser_is_param_char(args[i]) &&
                        mud_telnet_msp_parser_switch_on_param_char(&state, args, i, len))
                {
                    command->V = g_strdup(buffer->str);
                    g_string_free(buffer, TRUE);
                    buffer = g_string_new(NULL);
                }
                else
                    buffer = g_string_append_c(buffer, args[i]);
                break;

            case ARG_STATE_L:
                if(mud_telnet_msp_parser_is_param_char(args[i]) &&
                        mud_telnet_msp_parser_switch_on_param_char(&state, args, i, len))
                {
                    command->L = g_strdup(buffer->str);
                    g_string_free(buffer, TRUE);
                    buffer = g_string_new(NULL);
                }
                else
                    buffer = g_string_append_c(buffer, args[i]);
                break;

            case ARG_STATE_C:
                if(mud_telnet_msp_parser_is_param_char(args[i]) &&
                        mud_telnet_msp_parser_switch_on_param_char(&state, args, i, len))
                {
                    command->C = g_strdup(buffer->str);
                    g_string_free(buffer, TRUE);
                    buffer = g_string_new(NULL);
                }
                else
                    buffer = g_string_append_c(buffer, args[i]);
                break;

            case ARG_STATE_T:
                if(mud_telnet_msp_parser_is_param_char(args[i]) &&
                        mud_telnet_msp_parser_switch_on_param_char(&state, args, i, len))
                {
                    command->T = g_strdup(buffer->str);
                    g_string_free(buffer, TRUE);
                    buffer = g_string_new(NULL);
                }
                else
                    buffer = g_string_append_c(buffer, args[i]);
                break;

            case ARG_STATE_U:
                if(mud_telnet_msp_parser_is_param_char(args[i]) &&
                        mud_telnet_msp_parser_switch_on_param_char(&state, args, i, len))
                {
                    if(buffer->str[buffer->len - 1] != '/')
                        buffer = g_string_append_c(buffer, '/');

                    command->U = g_strdup(buffer->str);
                    g_string_free(buffer, TRUE);
                    buffer = g_string_new(NULL);
                }
                else
                    buffer = g_string_append_c(buffer, args[i]);
                break;

            case ARG_STATE_P:
                if(mud_telnet_msp_parser_is_param_char(args[i]) &&
                        mud_telnet_msp_parser_switch_on_param_char(&state, args, i, len))
                {
                    command->P = g_strdup(buffer->str);
                    g_string_free(buffer, TRUE);
                    buffer = g_string_new(NULL);
                }
                else
                    buffer = g_string_append_c(buffer, args[i]);
                break;
        }
    }

    switch(state)
    {
        case ARG_STATE_FILE:
            command->fName = g_strdup(buffer->str);
            break;

        case ARG_STATE_V:
            command->V = g_strdup(buffer->str);
            break;

        case ARG_STATE_L:
            command->L = g_strdup(buffer->str);
            break;

        case ARG_STATE_C:
            command->C = g_strdup(buffer->str);
            break;

        case ARG_STATE_T:
            command->T = g_strdup(buffer->str);
            break;

        case ARG_STATE_U:
            if(buffer->str[buffer->len - 1] != '/')
                buffer = g_string_append_c(buffer, '/');

            command->U = g_strdup(buffer->str);
            break;

        case ARG_STATE_P:
            command->P = g_strdup(buffer->str);
            break;
    }

    if(command->C)
        command->cont = atoi(command->C);

    if(command->T)
        command->sfx_type = g_strdup(command->T);

    if(command->V)
        command->volume = atoi(command->V);

    if(command->P)
        command->priority = atoi(command->P);

    if(command->L)
    {
        command->initial_repeat_count = atoi(command->L);

        if(command->initial_repeat_count == 0)
            command->initial_repeat_count = 1;

        command->current_repeat_count = command->initial_repeat_count;

        if(command->current_repeat_count == -1)
            command->loop = TRUE;
    }

    mud_telnet_msp_process_command(self, command);

    g_free(args);
    g_string_free(buffer, TRUE);
}

static gboolean
mud_telnet_msp_parser_is_param_char(gchar c)
{
    return (c == 'V' || c == 'L' || c == 'C' ||
            c == 'T' || c == 'U' || c == 'P');
}

static gboolean
mud_telnet_msp_parser_switch_on_param_char(gint *state, gchar *buf,
					   gint index, gint len)
{
    if(index + 1 == len)
        return FALSE;

    if(buf[index + 1] != '=')
        return FALSE;

    switch(buf[index])
    {
        case 'V':
            *state = ARG_STATE_V;
            return TRUE;
            break;

        case 'L':
            *state = ARG_STATE_L;
            return TRUE;
            break;

        case 'C':
            *state = ARG_STATE_C;
            return TRUE;
            break;

        case 'T':
            *state = ARG_STATE_T;
            return TRUE;
            break;

        case 'U':
            *state = ARG_STATE_U;
            return TRUE;
            break;

        case 'P':
            *state = ARG_STATE_P;
            return TRUE;
            break;
    }

    return FALSE;
}

static void
mud_telnet_msp_command_free(MudMSPCommand *command)
{
    if(command == NULL)
        return;

    if(command->fName)
        g_free(command->fName);

    if(command->mud_name)
        g_free(command->mud_name);

    if(command->sfx_type)
        g_free(command->sfx_type);

    if(command->V)
        g_free(command->V);

    if(command->L)
        g_free(command->L);

    if(command->P)
        g_free(command->P);

    if(command->C)
        g_free(command->C);

    if(command->T)
        g_free(command->T);

    if(command->U)
        g_free(command->U);

    g_free(command);

}

static void
mud_telnet_msp_process_command(MudTelnetMsp *self, MudMSPCommand *command)
{
    g_return_if_fail(MUD_IS_TELNET_MSP(self));

    g_log("Telnet", G_LOG_LEVEL_INFO, "MSP Command Parse Results");
    g_log("Telnet", G_LOG_LEVEL_INFO, 
            "Type: %s", (command->type == MSP_TYPE_SOUND) ? "Sound" :
            "Music" );
    g_log("Telnet", G_LOG_LEVEL_INFO,
            "Filename: %s", (command->fName != NULL) ? command->fName :
            "<null>");
    g_log("Telnet", G_LOG_LEVEL_INFO,
            "V: %s", (command->V != NULL) ? command->V : "<null>");
    g_log("Telnet", G_LOG_LEVEL_INFO,
            "L: %s", (command->L != NULL) ? command->L : "<null>");
    g_log("Telnet", G_LOG_LEVEL_INFO,
            "C: %s", (command->C != NULL) ? command->C : "<null>");
    g_log("Telnet", G_LOG_LEVEL_INFO, 
            "T: %s", (command->T != NULL) ? command->T : "<null>");
    g_log("Telnet", G_LOG_LEVEL_INFO,
            "U: %s", (command->U != NULL) ? command->U : "<null>");
    g_log("Telnet", G_LOG_LEVEL_INFO,
            "P: %s", (command->P != NULL) ? command->P : "<null>");
    g_log("Telnet", G_LOG_LEVEL_INFO,
            "Sfx Type: %s Volume: %d  Priority: %d  Repeat %d times. %s %s",
            (command->sfx_type) ? command->sfx_type:"None", command->volume,
            command->priority, command->initial_repeat_count, (command->loop) ?
            "Looping" : "Not Looping",
            (command->cont) ? "Continue" : "Stop");

    if(command->fName && strcmp(command->fName, "Off") == 0)
    {
        if(command->U)
        {
            if(self->priv->base_url)
                g_free(self->priv->base_url);

            self->priv->base_url = g_strdup(command->U);
        }
        else
            mud_telnet_msp_stop_playing(self, command->type);

        mud_telnet_msp_command_free(command);

        return;
    }

    if(self->priv->sound[command->type].current_command)
    {
        if(self->priv->sound[command->type].playing)
        {
            if(command->priority >
                   self->priv->sound[command->type].current_command->priority)
            {
                mud_telnet_msp_stop_playing(self, command->type);
                self->priv->sound[command->type].current_command = command;
                mud_telnet_msp_start_playing(self, command->type);
            }
            else
                mud_telnet_msp_command_free(command);
        }
        else
        {
            mud_telnet_msp_stop_playing(self, command->type);
            self->priv->sound[command->type].current_command = command;
            mud_telnet_msp_start_playing(self, command->type);
        }
    }
    else
    {
        self->priv->sound[command->type].current_command = command;
        mud_telnet_msp_start_playing(self, command->type);
    }
}

static void
mud_telnet_msp_start_playing(MudTelnetMsp *self, MudMSPTypes type)
{
    g_return_if_fail(MUD_IS_TELNET_MSP(self));

    if(!self->priv->sound[type].current_command)
        return;

    if(mud_telnet_msp_get_files(self, type))
    {
        gint num = 0;

        self->priv->sound[type].playing = TRUE;

        if(self->priv->sound[type].files_len != 0)
            num = rand() % self->priv->sound[type].files_len;

        self->priv->sound[type].play = gst_element_factory_make ("playbin", "play");
        g_object_set (G_OBJECT(self->priv->sound[type].play),
                "uri", self->priv->sound[type].files[num], NULL);
        g_object_set(G_OBJECT(self->priv->sound[type].play),
                "volume",
                (double)self->priv->sound[type].current_command->volume/100,
                NULL);

        self->priv->sound[type].bus =
            gst_pipeline_get_bus (GST_PIPELINE (self->priv->sound[type].play));

        if(type == MSP_TYPE_SOUND)
            gst_bus_add_watch (self->priv->sound[type].bus,
                    mud_telnet_msp_sound_bus_call, self);
        else
            gst_bus_add_watch (self->priv->sound[type].bus,
                    mud_telnet_msp_music_bus_call, self);

        gst_object_unref (self->priv->sound[type].bus);

        gst_element_set_state (self->priv->sound[type].play, GST_STATE_PLAYING);
    }
}

static gboolean
mud_telnet_msp_get_files(MudTelnetMsp *self, MudMSPTypes type)
{
    gchar sound_dir[2048];
    const gchar *file;
    gchar **files;
    gchar **structure;
    GString *file_output;
    GString *url_output;
    GString *file_name;
    GString *subdir;
    GString *full_dir;
    gchar *mud_name;
    GDir *dir;
    gint i, depth;
    GPatternSpec *regex;
    MudConnectionView *view;

    if(!MUD_IS_TELNET_MSP(self))
        return FALSE;

    g_object_get(self->priv->telnet, "parent-view", &view, NULL);
    g_object_get(view, "mud-name", &mud_name, NULL);

    if(!self->priv->sound[type].current_command)
        return FALSE;

    g_snprintf(sound_dir, 2048, "%s/.gnome-mud/audio/%s/",
            g_get_home_dir(), mud_name);
    if(!g_file_test(sound_dir, G_FILE_TEST_IS_DIR))
        g_mkdir_with_parents(sound_dir, 0777);

    g_free(mud_name);

    structure = g_strsplit(self->priv->sound[type].current_command->fName, "/", 0);
    depth = g_strv_length(structure);

    subdir = g_string_new(NULL);

    for(i = 0; i < depth - 1; ++i)
    {
        subdir = g_string_append(subdir, structure[i]);
        subdir = g_string_append_c(subdir, '/');
    }

    file_name = g_string_new(structure[depth - 1]);

    g_strfreev(structure);

    full_dir = g_string_new(sound_dir);
    full_dir = g_string_append(full_dir, subdir->str);

    if(self->priv->sound[type].current_command->T)
        full_dir = g_string_append(full_dir, self->priv->sound[type].current_command->T);

    if(!g_file_test(full_dir->str, G_FILE_TEST_IS_DIR))
        g_mkdir_with_parents(full_dir->str, 0777);

    file_output = g_string_new(NULL);

    regex = g_pattern_spec_new(file_name->str);

    dir = g_dir_open(full_dir->str, 0, NULL);

    while((file = g_dir_read_name(dir)) != NULL)
    {
        if(g_pattern_match_string(regex, file))
        {
            file_output = g_string_append(file_output, "file://");
            file_output = g_string_append(file_output, full_dir->str);
            file_output = g_string_append_c(file_output, '/');
            file_output = g_string_append(file_output, file);
            file_output = g_string_append_c(file_output, '\n');
        }
    }

    g_dir_close(dir);

    // Try searching again in main directory since
    // some servers ignore the standard concering the
    // T parameter and don't put the sound in a T-named
    // subdir.
    if(file_output->len == 0 && self->priv->sound[type].current_command->T)
    {
        g_string_free(full_dir, TRUE);
        full_dir = g_string_new(sound_dir);
        full_dir = g_string_append(full_dir, subdir->str);

        dir = g_dir_open(full_dir->str, 0, NULL);

        while((file = g_dir_read_name(dir)) != NULL)
        {
            if(g_pattern_match_string(regex, file))
            {
                file_output = g_string_append(file_output, "file://");
                file_output = g_string_append(file_output, full_dir->str);
                file_output = g_string_append_c(file_output, '/');
                file_output = g_string_append(file_output, file);
                file_output = g_string_append_c(file_output, '\n');
            }
        }

        g_dir_close(dir);
    }

    g_pattern_spec_free(regex);

    if(file_output->len == 0) // no matches, file doesn't exist.
    {
        url_output = g_string_new(NULL);

        if(self->priv->base_url || self->priv->sound[type].current_command->U)
        {
            if(self->priv->base_url)
                url_output = g_string_append(url_output, self->priv->base_url);
            else
                url_output = g_string_append(url_output, self->priv->sound[type].current_command->U);

            if(subdir->len != 0)
                url_output = g_string_append(url_output, subdir->str);

            /*	    if(self->priv->sound[type].current_command->T)
                    {
                    url_output = g_string_append(url_output, self->priv->sound[type].current_command->T);
                    url_output = g_string_append_c(url_output, '/');
                    }
                    */
            url_output = g_string_append(url_output, file_name->str);

            file_output = g_string_append(file_output, full_dir->str);
            if(self->priv->sound[type].current_command->T)
                file_output = g_string_append_c(file_output, '/');
            file_output = g_string_append(file_output, file_name->str);

            self->priv->sound[type].current_command->priority = 0;

            mud_connection_view_queue_download(view, url_output->str, file_output->str);
        }

        g_string_free(url_output, TRUE);
        g_string_free(file_output, TRUE);
        g_string_free(full_dir, TRUE);
        g_string_free(subdir, TRUE);
        g_string_free(file_name, TRUE);

        return FALSE;
    }

    files = g_strsplit(file_output->str, "\n", 0);

    if(self->priv->sound[type].files)
        g_strfreev(self->priv->sound[type].files);

    self->priv->sound[type].files = files;
    self->priv->sound[type].files_len = g_strv_length(files) - 1;

    g_string_free(file_output, TRUE);
    g_string_free(full_dir, TRUE);
    g_string_free(subdir, TRUE);
    g_string_free(file_name, TRUE);

    return TRUE;
}

static gboolean
mud_telnet_msp_sound_bus_call (GstBus *bus, GstMessage *msg, gpointer data)
{
    MudTelnetMsp *self = MUD_TELNET_MSP(data);

    switch (GST_MESSAGE_TYPE (msg))
    {
        case GST_MESSAGE_EOS:
            self->priv->sound[MSP_TYPE_SOUND].playing = FALSE;

            self->priv->sound[MSP_TYPE_SOUND].current_command->current_repeat_count--;

            gst_element_set_state (self->priv->sound[MSP_TYPE_SOUND].play, GST_STATE_NULL);

            if(self->priv->sound[MSP_TYPE_SOUND].current_command->loop ||
                    self->priv->sound[MSP_TYPE_SOUND].current_command->current_repeat_count != 0)
            {
                gint num = 0;

                if(self->priv->sound[MSP_TYPE_SOUND].files_len != 0)
                    num = rand() % self->priv->sound[MSP_TYPE_SOUND].files_len;

                g_object_set (G_OBJECT(self->priv->sound[MSP_TYPE_SOUND].play),
                        "uri", self->priv->sound[MSP_TYPE_SOUND].files[num], NULL);
                g_object_set(G_OBJECT(self->priv->sound[MSP_TYPE_SOUND].play),
                        "volume", (double)self->priv->sound[MSP_TYPE_SOUND].current_command->volume/100.0, NULL);

                gst_element_set_state (self->priv->sound[MSP_TYPE_SOUND].play, GST_STATE_PLAYING);
            }
            else
                mud_telnet_msp_stop_playing(self, MSP_TYPE_SOUND);
            break;

        case GST_MESSAGE_ERROR:
            {
                gchar *debug;
                GError *err;

                gst_message_parse_error (msg, &err, &debug);
                g_free (debug);

                g_warning ("Error: %s", err->message);
                g_error_free (err);

                break;
            }

        default:
            break;
    }

    return TRUE;
}

static gboolean
mud_telnet_msp_music_bus_call (GstBus *bus, GstMessage *msg, gpointer data)
{
    MudTelnetMsp *self = MUD_TELNET_MSP(data);

    switch (GST_MESSAGE_TYPE (msg))
    {
        case GST_MESSAGE_EOS:
            self->priv->sound[MSP_TYPE_MUSIC].playing = FALSE;

            self->priv->sound[MSP_TYPE_MUSIC].current_command->current_repeat_count--;

            gst_element_set_state (self->priv->sound[MSP_TYPE_MUSIC].play, GST_STATE_NULL);

            if(self->priv->sound[MSP_TYPE_MUSIC].current_command->loop ||
                    self->priv->sound[MSP_TYPE_MUSIC].current_command->current_repeat_count != 0)
            {
                gint num = 0;

                if(self->priv->sound[MSP_TYPE_MUSIC].files_len != 0)
                    num = rand() % self->priv->sound[MSP_TYPE_MUSIC].files_len;

                g_object_set (G_OBJECT(self->priv->sound[MSP_TYPE_MUSIC].play),
                        "uri", self->priv->sound[MSP_TYPE_MUSIC].files[num], NULL);
                g_object_set(G_OBJECT(self->priv->sound[MSP_TYPE_MUSIC].play),
                        "volume", (double)self->priv->sound[MSP_TYPE_MUSIC].current_command->volume/100.0, NULL);

                gst_element_set_state (self->priv->sound[MSP_TYPE_MUSIC].play, GST_STATE_PLAYING);
            }
            else
                mud_telnet_msp_stop_playing(self, MSP_TYPE_MUSIC);

            break;

        case GST_MESSAGE_ERROR:
            {
                gchar *debug;
                GError *err;

                gst_message_parse_error (msg, &err, &debug);
                g_free (debug);

                g_warning ("Error: %s", err->message);
                g_error_free (err);

                break;
            }

        default:
            break;
    }

    return TRUE;
}

#endif


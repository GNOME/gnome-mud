/* GNOME-Mud - A simple Mud Client
 * mud-trigger.c
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

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include <string.h>
#include <stdlib.h>

#include "mud-line-buffer.h"
#include "mud-trigger.h"
#include "mud-subwindow.h"
#include "gnome-mud-builtins.h"
#include "utils.h"

struct _MudTriggerPrivate
{
    MudTriggerType type;
    MudLineBuffer *line_buffer;

    gulong lines;

    gboolean enabled;
    gboolean gag;
    
    /* Simple Text Triggers */
    GRegex *simple_regex;
    gchar *match_string;

    /* Glob Triggers */
    GPatternSpec *glob;
    gchar *glob_string;

    /* Regex Triggers */
    GRegex *regex;
    GMatchInfo *info;
    gchar *regex_string;

    /* Condition Triggers */
    GList *condition_items;

    /* Filter */
    gboolean filter;
    GList *filter_set;
    guint filter_line;
    gboolean filter_active_step;

    /* Key names */
    gchar *profile_key;
    gchar *trigger_key;

    /* Action */
    gchar *action;
    MudTriggerAction action_type;

    /* Variables */
    GList *variables;

    /* Subwindow */
    GList *windows;
    gchar *title;
    guint width;
    guint height;
    gboolean input;
    gboolean scroll;
};

enum
{
    PARSE_STATE_TEXT,
    PARSE_STATE_REGISTER
};

/* Property Identifiers */
enum
{
    PROP_MUD_TRIGGER_0,
    PROP_TRIGGER_KEY,
    PROP_PROFILE_KEY,
    PROP_LINES,
    PROP_ACTION,
    PROP_ACTION_TYPE
};

/* Create the Type */
G_DEFINE_TYPE(MudTrigger, mud_trigger, G_TYPE_OBJECT);

/* Class Functions */
static void mud_trigger_init (MudTrigger *self);
static void mud_trigger_class_init (MudTriggerClass *klass);
static void mud_trigger_finalize (GObject *object);
static GObject *mud_trigger_constructor (GType gtype,
                                         guint n_properties,
                                         GObjectConstructParam *properties);
static void mud_trigger_set_property(GObject *object,
                                     guint prop_id,
                                     const GValue *value,
                                     GParamSpec *pspec);
static void mud_trigger_get_property(GObject *object,
                                     guint prop_id,
                                     GValue *value,
                                     GParamSpec *pspec);

// Callbacks
static void mud_trigger_line_added_cb(MudLineBuffer *buffer,
                                      MudLineBufferLine *line,
                                      MudTrigger *self);

// Private Methods
static void   mud_trigger_do_action(MudTrigger *self);
static gchar *mud_trigger_parse(MudTrigger *self, const gchar *data);

/* MudTrigger class functions */
static void
mud_trigger_class_init (MudTriggerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    /* Override base object constructor */
    object_class->constructor = mud_trigger_constructor;

    /* Override base object's finalize */
    object_class->finalize = mud_trigger_finalize;

    /* Override base object property methods */
    object_class->set_property = mud_trigger_set_property;
    object_class->get_property = mud_trigger_get_property;

    /* Add private data to class */
    g_type_class_add_private(klass, sizeof(MudTriggerPrivate));

    /* Install Properties */
    g_object_class_install_property(object_class,
            PROP_TRIGGER_KEY,
            g_param_spec_string("trigger-key",
                                "Trigger Key",
                                "The Trigger's GConf Key",
                                NULL,
                                G_PARAM_READWRITE|G_PARAM_CONSTRUCT));

    g_object_class_install_property(object_class,
            PROP_PROFILE_KEY,
            g_param_spec_string("profile-key",
                                "Profile Key",
                                "The Profile's GConf Key",
                                NULL,
                                G_PARAM_READWRITE|G_PARAM_CONSTRUCT));

    g_object_class_install_property(object_class,
            PROP_LINES,
            g_param_spec_ulong("lines",
                               "Lines",
                               "The number of lines the trigger should check.",
                               0,
                               G_MAXULONG,
                               1,
                               G_PARAM_READWRITE|G_PARAM_CONSTRUCT));

    g_object_class_install_property(object_class,
            PROP_ACTION,
            g_param_spec_string("action",
                                "Action",
                                "The Trigger's action",
                                NULL,
                                G_PARAM_READWRITE|G_PARAM_CONSTRUCT));

    g_object_class_install_property(object_class,
            PROP_ACTION_TYPE,
            g_param_spec_enum("action-type",
                              "Action Type",
                              "The Trigger's action type",
                              MUD_TYPE_TRIGGER_ACTION,
                              MUD_TRIGGER_ACTION_TEXT,
                              G_PARAM_READWRITE|G_PARAM_CONSTRUCT));
}

static void
mud_trigger_init (MudTrigger *self)
{
    /* Get our private data */
    self->priv = MUD_TRIGGER_GET_PRIVATE(self);

    /* Set Defaults */
    self->priv->type = MUD_TRIGGER_TYPE_SINGLE;

    self->priv->line_buffer = NULL;

    self->priv->gag = FALSE;

    self->priv->glob = NULL;
    self->priv->glob_string = NULL;

    self->priv->regex = NULL;
    self->priv->regex_string = NULL;

    self->priv->condition_items = NULL;

    self->priv->trigger_key = NULL;
    self->priv->profile_key = NULL;

    self->priv->windows = NULL;

    self->priv->filter_set = NULL;
}

static GObject *
mud_trigger_constructor (GType gtype,
                         guint n_properties,
                         GObjectConstructParam *properties)
{
    MudTrigger *self;
    GObject *obj;
    MudTriggerClass *klass;
    GObjectClass *parent_class;

    /* Chain up to parent constructor */
    klass = MUD_TRIGGER_CLASS( g_type_class_peek(MUD_TYPE_TRIGGER) );
    parent_class = G_OBJECT_CLASS( g_type_class_peek_parent(klass) );
    obj = parent_class->constructor(gtype, n_properties, properties);

    self = MUD_TRIGGER(obj);

    if(!self->priv->trigger_key)
        g_error("MudTrigger instatiated without trigger's key name.");

    if(!self->priv->profile_key)
        g_error("MudTrigger instantiated without profile's key name.");

    self->priv->line_buffer = g_object_new(MUD_TYPE_LINE_BUFFER,
                                           "maximum-line-count", self->priv->lines,
                                           NULL);

    g_signal_connect(self->priv->line_buffer,
                     "line-added",
                     G_CALLBACK(mud_trigger_line_added_cb),
                     self);

    g_printf("Action: %s\n", self->priv->action);
    g_printf("Action Type: %d\n", self->priv->action_type);

    self->priv->type = MUD_TRIGGER_TYPE_SINGLE;
    /*self->priv->regex = g_regex_new("^(.*) says, \n\"(.*)\"",
                                    G_REGEX_OPTIMIZE|G_REGEX_MULTILINE|G_REGEX_DOTALL,
                                    0,
                                    NULL);*/

    //self->priv->glob = g_pattern_spec_new("foo?*bar*");
   
    /*self->priv->simple_regex = g_regex_new("hello world",
                                           G_REGEX_OPTIMIZE,
                                           0,
                                           NULL);*/

    self->priv->regex = g_regex_new("^(.*) says, \"(.*)\"",
                                    G_REGEX_OPTIMIZE,
                                    0,
                                    NULL);
    return obj;
}

static void
mud_trigger_finalize (GObject *object)
{
    MudTrigger *self;
    GObjectClass *parent_class;

    self = MUD_TRIGGER(object);

    g_object_unref(self->priv->line_buffer);

    if(self->priv->regex)
        g_regex_unref(self->priv->regex);

    if(self->priv->glob)
        g_pattern_spec_free(self->priv->glob);

    g_free(self->priv->trigger_key);
    g_free(self->priv->profile_key);

    parent_class = g_type_class_peek_parent(G_OBJECT_GET_CLASS(object));
    parent_class->finalize(object);
}

static void
mud_trigger_set_property(GObject *object,
                         guint prop_id,
                         const GValue *value,
                         GParamSpec *pspec)
{
    MudTrigger *self;
    gchar *new_string;
    gulong new_ulong;
    MudTriggerAction new_action;

    self = MUD_TRIGGER(object);

    switch(prop_id)
    {
        case PROP_TRIGGER_KEY:
            new_string = g_value_dup_string(value);

            if(!self->priv->trigger_key)
                self->priv->trigger_key = g_strdup(new_string);
            else if( !g_str_equal(self->priv->trigger_key, new_string) )
            {
                g_free(self->priv->trigger_key);
                self->priv->trigger_key = g_strdup(new_string);
            }

            g_free(new_string);
            break;

        case PROP_PROFILE_KEY:
            new_string = g_value_dup_string(value);

            if(!self->priv->profile_key)
                self->priv->profile_key = g_strdup(new_string);
            else if( !g_str_equal(self->priv->profile_key, new_string) )
            {
                g_free(self->priv->profile_key);
                self->priv->profile_key = g_strdup(new_string);
            }

            g_free(new_string);
            break;

        case PROP_ACTION:
            new_string = g_value_dup_string(value);

            if(!self->priv->action)
                self->priv->action = g_strdup(new_string);
            else if( !g_str_equal(self->priv->action, new_string) )
            {
                g_free(self->priv->action);
                self->priv->action = g_strdup(new_string);
            }

            g_free(new_string);
            break;

        case PROP_ACTION_TYPE:
            new_action = g_value_get_enum(value);

            if(new_action != self->priv->action_type)
                self->priv->action_type = new_action;
            break;

        case PROP_LINES:
            new_ulong = g_value_get_ulong(value);

            if(new_ulong != self->priv->lines)
                self->priv->lines = new_ulong;
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void
mud_trigger_get_property(GObject *object,
                         guint prop_id,
                         GValue *value,
                         GParamSpec *pspec)
{
    MudTrigger *self;

    self = MUD_TRIGGER(object);

    switch(prop_id)
    {
        case PROP_TRIGGER_KEY:
            g_value_set_string(value, self->priv->trigger_key);
            break;

        case PROP_PROFILE_KEY:
            g_value_set_string(value, self->priv->profile_key);
            break;

        case PROP_LINES:
            g_value_set_ulong(value, self->priv->lines);
            break;

        case PROP_ACTION:
            g_value_set_string(value, self->priv->action);
            break;

        case PROP_ACTION_TYPE:
            g_value_set_enum(value, self->priv->action_type);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

// Public Methods
void
mud_trigger_add_data(MudTrigger *self,
                     const gchar *data,
                     guint length)
{
    gchar *stripped;

    g_return_if_fail(MUD_IS_TRIGGER(self));

    stripped = utils_strip_ansi(data);

    mud_line_buffer_add_data(self->priv->line_buffer,
                             stripped,
                             strlen(stripped));

    g_free(stripped);
}

void
mud_trigger_execute(MudTrigger *self,
                    MudLineBufferLine *line,
                    guint length)
{
    gchar *stripped = utils_strip_ansi(line->line->str);
    guint len = strlen(stripped);

    switch(self->priv->type)
    {
        case MUD_TRIGGER_TYPE_SINGLE:
            if(self->priv->regex) // Regex match
            {
                if(g_regex_match_full(self->priv->regex,
                                      stripped,
                                      len,
                                      0,
                                      0,
                                      &self->priv->info,
                                      NULL))
                {
                    mud_trigger_do_action(self);

                    if(self->priv->gag)
                        line->gag = TRUE;

                    g_match_info_free(self->priv->info);
                }
            }
            else if(self->priv->glob) // Glob match
            {
                if(g_pattern_match_string(self->priv->glob,
                                          stripped))
                {
                    mud_trigger_do_action(self);

                    if(self->priv->gag)
                        line->gag = TRUE;
                }
            }
            else if(self->priv->simple_regex) // Simple text match.
            {
                if(g_regex_match_full(self->priv->simple_regex,
                                      stripped,
                                      len,
                                      0,
                                      0,
                                      &self->priv->info,
                                      NULL))
                {
                    mud_trigger_do_action(self);

                    if(self->priv->gag)
                        line->gag = TRUE;

                    g_match_info_free(self->priv->info);
                } 
            }
            else
                g_return_if_reached();

            break;

        case MUD_TRIGGER_TYPE_CONDITION:
            break;

        default:
            g_warn_if_reached();
            break;
    }

    g_free(stripped);
}

void
mud_trigger_add_filter_child(MudTrigger *self,
                             MudTrigger *child)
{
    g_return_if_fail(MUD_IS_TRIGGER(self));
    g_return_if_fail(MUD_IS_TRIGGER(child));

    self->priv->filter_set = g_list_append(self->priv->filter_set,
                                           child);
}

void
mud_trigger_add_window(MudTrigger *self,
                       const gchar *title,
                       guint width,
                       guint height,
                       gboolean input,
                       gboolean scroll)
{

}

// Callbacks
static void
mud_trigger_line_added_cb(MudLineBuffer *buffer,
                          MudLineBufferLine *line,
                          MudTrigger *self)
{
    gulong len, max;
    
    switch(self->priv->type)
    {
        case MUD_TRIGGER_TYPE_MULTI:
            g_object_get(self->priv->line_buffer,
                         "length", &len,
                         "maximum-line-count", &max,
                         NULL);

            if(len == max)
            {
                gchar *lines = mud_line_buffer_get_lines(self->priv->line_buffer);
                guint lines_length = strlen(lines);

                if(self->priv->regex) // Regex match
                {
                    if(g_regex_match_full(self->priv->regex,
                                lines,
                                lines_length,
                                0,
                                0,
                                &self->priv->info,
                                NULL))
                    {
                        mud_trigger_do_action(self);

                        g_match_info_free(self->priv->info);
                    }

                }
                else if(self->priv->glob) // Glob match
                {
                    if(g_pattern_match_string(self->priv->glob,
                                lines))
                    {
                        mud_trigger_do_action(self);
                    }
                }
                else if(self->priv->simple_regex) // Simple text match.
                {
                   if(g_regex_match_full(self->priv->simple_regex,
                                         lines,
                                         lines_length,
                                         0,
                                         0,
                                         &self->priv->info,
                                         NULL))
                   {
                       mud_trigger_do_action(self);

                       g_match_info_free(self->priv->info);
                   } 
                }
                else
                    g_return_if_reached();

                g_free(lines);
            }
            break;

        default:
            g_warn_if_reached();
            break;
    }
}

// Private Methods
static void
mud_trigger_do_action(MudTrigger *self)
{
    switch(self->priv->action_type)
    {
        case MUD_TRIGGER_ACTION_TEXT:
            if(self->priv->action)
            {
               if(self->priv->regex)
               {
                   gchar *data;

                   data = mud_trigger_parse(self,
                                            self->priv->action);

                   // send to mud here
                   g_printf("Parsed: %s\n", data);

                   g_free(data);
               }
               else
               {
                   // send to mud here
                   g_printf("Action Fired: %s\n", self->priv->action);
               }
            }
            break;

        case MUD_TRIGGER_ACTION_VAR:
            break;

        case MUD_TRIGGER_ACTION_LUA:
            // coming in 0.13
            break;
    }
}

static gchar * 
mud_trigger_parse(MudTrigger *self, const gchar *data)
{
    gint state;
    gchar **matches;
    GString *ret, *reg_num;
    guint length, matches_length, i, j, num;

    length = strlen(data);

    if(length == 0)
        return NULL;

    ret = g_string_new(NULL);
    reg_num = NULL;

    matches = g_match_info_fetch_all(self->priv->info);
    matches_length = g_strv_length(matches);

    state = PARSE_STATE_TEXT;

    for(i = 0; i < length; i++)
    {
        switch(state)
        {
            case PARSE_STATE_TEXT:
                if(data[i] == '%' && i + 1 < length)
                    state = PARSE_STATE_REGISTER;
                else
                    ret = g_string_append_c(ret, data[i]);
                break;

            case PARSE_STATE_REGISTER:
                reg_num = g_string_new(NULL);

                j = i;

                while(TRUE)
                {
                    if(j == length || data[j] != '%')
                        break;

                    ret = g_string_append_c(ret, data[j++]);
                }

                if(j == length)
                {
                    if(data[j - 1] == '%')
                        ret = g_string_append_c(ret, data[j - 1]);

                    i = j;
                    break;
                }

                for(; g_ascii_isdigit(data[j]) && j < length; j++)
                    reg_num = g_string_append_c(reg_num, data[j]);

                if(reg_num->len == 0) // No number, not a register.
                {
                    /* Append the % that got us here. */
                    ret = g_string_append_c(ret, '%');

                    i = j - 1;

                    g_string_free(reg_num, TRUE);

                    state = PARSE_STATE_TEXT;
                }
                else
                {
                    i = j - 1;

                    num = atol(reg_num->str);

                    g_string_free(reg_num, TRUE);

                    if(num >= matches_length)
                        ret = g_string_append(ret, _("#Submatch Out of Range#"));
                    else
                        ret = g_string_append(ret, matches[num]);

                    state = PARSE_STATE_TEXT;
                }
                break;
        }
    }

    if(matches_length != 0)
        g_strfreev(matches);

    return g_string_free(ret, (ret->len == 0) );
}


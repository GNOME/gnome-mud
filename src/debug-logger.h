/* Debug Logger - A UI for Log Messages
 * debug-logger.h
 * Copyright (C) 2009 Les Harris <lharris@gnome.org>
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


#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <glib.h>

G_BEGIN_DECLS

#define TYPE_DEBUG_LOGGER              (debug_logger_get_type ())
#define DEBUG_LOGGER(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), TYPE_DEBUG_LOGGER, DebugLogger))
#define DEBUG_LOGGER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_DEBUG_LOGGER, DebugLoggerClass))
#define IS_DEBUG_LOGGER(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), TYPE_DEBUG_LOGGER))
#define IS_DEBUG_LOGGER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_DEBUG_LOGGER))
#define DEBUG_LOGGER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), TYPE_DEBUG_LOGGER, DebugLoggerClass))
#define DEBUG_LOGGER_GET_PRIVATE(obj)  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), TYPE_DEBUG_LOGGER, DebugLoggerPrivate))

typedef struct _DebugLogger          DebugLogger;
typedef struct _DebugLoggerClass     DebugLoggerClass;
typedef struct _DebugLoggerPrivate   DebugLoggerPrivate;

struct _DebugLoggerClass
{
    GObjectClass parent_class;

    /* Class Members */

    /* Signals */
    void (*critical_received) (DebugLogger *logger, gchar *message);
    void (*warning_received)  (DebugLogger *logger, gchar *message);
    void (*message_received)  (DebugLogger *logger, gchar *message);
    void (*info_received)     (DebugLogger *logger, gchar *message);
    void (*debug_received)    (DebugLogger *logger, gchar *message);
    void (*unknown_received)  (DebugLogger *logger, gchar *message);

};

struct _DebugLogger
{
    GObject parent_instance;

    /*< private >*/
    DebugLoggerPrivate *priv;

    /*< public >*/
    gchar *critical_color;
    gchar *warning_color;
    gchar *message_color;
    gchar *info_color;
    gchar *debug_color;
    gchar *unknown_color;
    gboolean use_color;
    gboolean ui_enabled;
    gboolean closeable;
};

/* Methods */
GType debug_logger_get_type (void);

void debug_logger_create_window(DebugLogger *logger);

void debug_logger_destroy_window(DebugLogger *logger);

void debug_logger_add_domain(DebugLogger *logger, 
                             const gchar *domain_name,
                             gboolean default_domain);

void debug_logger_add_standard_domains(DebugLogger *logger);

void debug_logger_remove_domain(DebugLogger *logger,
                                const gchar *domain_name);
G_END_DECLS

#endif // DEBUG_LOG_H


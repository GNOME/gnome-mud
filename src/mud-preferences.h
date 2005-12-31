#ifndef MUD_PREFERENCES_H
#define MUD_PREFERENCES_H

G_BEGIN_DECLS

#include <gconf/gconf-client.h>
#include "mud-profile.h"

#define MUD_TYPE_PREFERENCES              (mud_preferences_get_type ())
#define MUD_PREFERENCES(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), MUD_TYPE_PREFERENCES, MudPreferences))
#define MUD_PREFERENCES_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), MUD_TYPE_PREFERENCES, MudPreferencesClass))
#define MUD_IS_PREFERENCES(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), MUD_TYPE_PREFERENCES))
#define MUD_IS_PREFERENCES_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), MUD_TYPE_PREFERENCES))
#define MUD_PREFERENCES_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), MUD_TYPE_PREFERENCES, MudPreferenesClass))

typedef struct _MudPreferences            MudPreferences;
typedef struct _MudPreferencesClass       MudPreferencesClass;
typedef struct _MudPreferencesPrivate     MudPreferencesPrivate;

struct _MudPreferences
{
	GObject parent_instance;

	MudPreferencesPrivate *priv;

	MudPrefs preferences;
};

typedef struct
{
	unsigned int EchoText : 1;
	unsigned int KeepText : 1;
	unsigned int DisableKeys : 1;
	unsigned int ScrollOnOutput : 1;
	unsigned int CommDev : 1;
	unsigned int TerminalType : 1;
	unsigned int History : 1;
	unsigned int Scrollback : 1;
	unsigned int FontName : 1;
	unsigned int Foreground : 1;
	unsigned int Background : 1;
	unsigned int Colors : 1;
} MudPreferenceMask;

struct _MudPreferencesClass
{
	GObjectClass parent_class;

	void (* changed) (MudPreferences *prefs, MudPreferenceMask *mask, gpointer data);
};

GType mud_preferences_get_type (void) G_GNUC_CONST;

MudPreferences* mud_preferences_new (GConfClient *client);
const GList *mud_preferences_get_profiles (MudPreferences *prefs);

void mud_preferences_set_echotext (MudPreferences *prefs, gboolean value);
void mud_preferences_set_keeptext (MudPreferences *prefs, gboolean value);
void mud_preferences_set_disablekeys (MudPreferences *prefs, gboolean value);
void mud_preferences_set_scrolloutput (MudPreferences *prefs, gboolean value);
void mud_preferences_set_commdev (MudPreferences *prefs, const gchar *value);
void mud_preferences_set_terminal (MudPreferences *prefs, const gchar *value);
void mud_preferences_set_history (MudPreferences *prefs, const gint value);
void mud_preferences_set_scrollback (MudPreferences *prefs, const gint value);
void mud_preferences_set_font (MudPreferences *prefs, const gchar *value);
void mud_preferences_set_foreground (MudPreferences *prefs, guint r, guint g, guint b);
void mud_preferences_set_background (MudPreferences *prefs, guint r, guint g, guint b);
void mud_preferences_set_colors (MudPreferences *prefs, gint nr, guint r, guint g, guint b);

G_END_DECLS

#endif // MUD_PREFERENCES_H
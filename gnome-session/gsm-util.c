/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 * gsm-util.c
 * Copyright (C) 2008 Lucas Rocha.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "gsm-util.h"

static gchar *_saved_session_dir = NULL;
static gchar **child_environment;

char *
gsm_util_find_desktop_file_for_app_name (const char *name,
                                         gboolean    look_in_saved_session,
                                         gboolean    autostart_first)
{
        char     *app_path;
        char    **app_dirs;
        GKeyFile *key_file;
        char     *desktop_file;
        int       i;

        app_path = NULL;

        app_dirs = gsm_util_get_desktop_dirs (look_in_saved_session, autostart_first);

        key_file = g_key_file_new ();

        desktop_file = g_strdup_printf ("%s.desktop", name);

        g_debug ("GsmUtil: Looking for file '%s'", desktop_file);

        for (i = 0; app_dirs[i] != NULL; i++) {
                g_debug ("GsmUtil: Looking in '%s'", app_dirs[i]);
        }

        g_key_file_load_from_dirs (key_file,
                                   desktop_file,
                                   (const char **) app_dirs,
                                   &app_path,
                                   G_KEY_FILE_NONE,
                                   NULL);

        if (app_path != NULL) {
                g_debug ("GsmUtil: found in XDG dirs: '%s'", app_path);
        }

        /* look for gnome vendor prefix */
        if (app_path == NULL) {
                g_free (desktop_file);
                desktop_file = g_strdup_printf ("gnome-%s.desktop", name);

                g_key_file_load_from_dirs (key_file,
                                           desktop_file,
                                           (const char **) app_dirs,
                                           &app_path,
                                           G_KEY_FILE_NONE,
                                           NULL);
                if (app_path != NULL) {
                        g_debug ("GsmUtil: found in XDG dirs: '%s'", app_path);
                }
        }

        g_free (desktop_file);
        g_key_file_free (key_file);

        g_strfreev (app_dirs);

        return app_path;
}

static gboolean
ensure_dir_exists (const char *dir)
{
        if (g_mkdir_with_parents (dir, 0755) == 0)
                return TRUE;

        g_warning ("GsmSessionSave: Failed to create directory %s: %s", dir, strerror (errno));

        return FALSE;
}

gchar *
gsm_util_get_empty_tmp_session_dir (void)
{
        char *tmp;
        gboolean exists;

        tmp = g_build_filename (g_get_user_config_dir (),
                                "gnome-session",
                                "saved-session.new",
                                NULL);

        exists = ensure_dir_exists (tmp);

        if (G_UNLIKELY (!exists)) {
                g_warning ("GsmSessionSave: could not create directory for saved session: %s", tmp);
                g_free (tmp);
                return NULL;
        } else {
                /* make sure it's empty */
                GDir       *dir;
                const char *filename;

                dir = g_dir_open (tmp, 0, NULL);
                if (dir) {
                        while ((filename = g_dir_read_name (dir))) {
                                char *path = g_build_filename (tmp, filename,
                                                               NULL);
                                g_unlink (path);
                        }
                        g_dir_close (dir);
                }
        }

        return tmp;
}

const gchar *
gsm_util_get_saved_session_dir (void)
{
        if (_saved_session_dir == NULL) {
                gboolean exists;

                _saved_session_dir =
                        g_build_filename (g_get_user_config_dir (),
                                          "gnome-session",
                                          "saved-session",
                                          NULL);

                exists = ensure_dir_exists (_saved_session_dir);

                if (G_UNLIKELY (!exists)) {
                        static gboolean printed_warning = FALSE;

                        if (!printed_warning) {
                                g_warning ("GsmSessionSave: could not create directory for saved session: %s", _saved_session_dir);
                                printed_warning = TRUE;
                        }

                        _saved_session_dir = NULL;

                        return NULL;
                }
        }

        return _saved_session_dir;
}

static char ** autostart_dirs;

void
gsm_util_set_autostart_dirs (char ** dirs)
{
        autostart_dirs = g_strdupv (dirs);
}

static char **
gsm_util_get_standard_autostart_dirs (void)
{
        GPtrArray          *dirs;
        const char * const *system_config_dirs;
        const char * const *system_data_dirs;
        int                 i;

        dirs = g_ptr_array_new ();

        g_ptr_array_add (dirs,
                         g_build_filename (g_get_user_config_dir (),
                                           "autostart", NULL));

        system_data_dirs = g_get_system_data_dirs ();
        for (i = 0; system_data_dirs[i]; i++) {
                g_ptr_array_add (dirs,
                                 g_build_filename (system_data_dirs[i],
                                                   "gnome", "autostart", NULL));
        }

        system_config_dirs = g_get_system_config_dirs ();
        for (i = 0; system_config_dirs[i]; i++) {
                g_ptr_array_add (dirs,
                                 g_build_filename (system_config_dirs[i],
                                                   "autostart", NULL));
        }

        g_ptr_array_add (dirs, NULL);

        return (char **) g_ptr_array_free (dirs, FALSE);
}

char **
gsm_util_get_autostart_dirs ()
{
        if (autostart_dirs) {
                return g_strdupv ((char **)autostart_dirs);
        }

        return gsm_util_get_standard_autostart_dirs ();
}

char **
gsm_util_get_app_dirs ()
{
        GPtrArray          *dirs;
        const char * const *system_data_dirs;
        int                 i;

        dirs = g_ptr_array_new ();

        g_ptr_array_add (dirs,
                         g_build_filename (g_get_user_data_dir (),
                                           "applications",
                                           NULL));

        system_data_dirs = g_get_system_data_dirs ();
        for (i = 0; system_data_dirs[i]; i++) {
                g_ptr_array_add (dirs,
                                 g_build_filename (system_data_dirs[i],
                                                   "applications",
                                                   NULL));
        }

        g_ptr_array_add (dirs, NULL);

        return (char **) g_ptr_array_free (dirs, FALSE);
}

char **
gsm_util_get_desktop_dirs (gboolean include_saved_session,
                           gboolean autostart_first)
{
        char **apps;
        char **autostart;
        char **standard_autostart;
        char **result;
        int    size;
        int    i;

        apps = gsm_util_get_app_dirs ();
        autostart = gsm_util_get_autostart_dirs ();

        /* Still, check the standard autostart dirs for things like fulfilling session reqs,
         * if using a non-standard autostart dir for autostarting */
        if (autostart_dirs != NULL)
                standard_autostart = gsm_util_get_standard_autostart_dirs ();
        else
                standard_autostart = NULL;

        size = 0;
        for (i = 0; apps[i] != NULL; i++) { size++; }
        for (i = 0; autostart[i] != NULL; i++) { size++; }
        if (autostart_dirs != NULL)
                for (i = 0; standard_autostart[i] != NULL; i++) { size++; }
        if (include_saved_session)
                size += 1;

        result = g_new (char *, size + 1); /* including last NULL */

        size = 0;

        if (autostart_first) {
                if (include_saved_session)
                        result[size++] = g_strdup (gsm_util_get_saved_session_dir ());

                for (i = 0; autostart[i] != NULL; i++, size++) {
                        result[size] = autostart[i];
                }
                if (standard_autostart != NULL) {
                        for (i = 0; standard_autostart[i] != NULL; i++, size++) {
                                result[size] = standard_autostart[i];
                        }
                }
                for (i = 0; apps[i] != NULL; i++, size++) {
                        result[size] = apps[i];
                }
        } else {
                for (i = 0; apps[i] != NULL; i++, size++) {
                        result[size] = apps[i];
                }
                if (standard_autostart != NULL) {
                        for (i = 0; standard_autostart[i] != NULL; i++, size++) {
                                result[size] = standard_autostart[i];
                        }
                }
                for (i = 0; autostart[i] != NULL; i++, size++) {
                        result[size] = autostart[i];
                }

                if (include_saved_session)
                        result[size++] = g_strdup (gsm_util_get_saved_session_dir ());
        }

        g_free (apps);
        g_free (autostart);
        g_free (standard_autostart);

        result[size] = NULL;

        return result;
}

gboolean
gsm_util_text_is_blank (const char *str)
{
        if (str == NULL) {
                return TRUE;
        }

        while (*str) {
                if (!isspace(*str)) {
                        return FALSE;
                }

                str++;
        }

        return TRUE;
}

/**
 * gsm_util_init_error:
 * @fatal: whether or not the error is fatal to the login session
 * @format: printf-style error message format
 * @...: error message args
 *
 * Displays the error message to the user. If @fatal is %TRUE, gsm
 * will exit after displaying the message.
 *
 * This should be called for major errors that occur before the
 * session is up and running. (Notably, it positions the dialog box
 * itself, since no window manager will be running yet.)
 **/
void
gsm_util_init_error (gboolean    fatal,
                     const char *format, ...)
{
        char           *msg;
        va_list         args;
        gchar          *argv[13];

        va_start (args, format);
        msg = g_strdup_vprintf (format, args);
        va_end (args);

        argv[0] = "zenity";
        argv[1] = "--error";
        argv[2] = "--class";
        argv[3] = "mutter-dialog";
        argv[4] = "--title";
        argv[5] = "\"\"";
        argv[6] = "--text";
        argv[7] = msg;
        argv[8] = "--icon-name";
        argv[9] = "face-sad-symbolic";
        argv[10] = "--ok-label";
        argv[11] = _("_Log out");
        argv[12] = NULL;

        g_spawn_sync (NULL, argv, child_environment, 0, NULL, NULL, NULL, NULL, NULL, NULL);

        g_free (msg);

        if (fatal) {
                exit (1);
        }
}

/**
 * gsm_util_generate_startup_id:
 *
 * Generates a new SM client ID.
 *
 * Return value: an SM client ID.
 **/
char *
gsm_util_generate_startup_id (void)
{
        static int     sequence = -1;
        static guint   rand1 = 0;
        static guint   rand2 = 0;
        static pid_t   pid = 0;
        struct timeval tv;

        /* The XSMP spec defines the ID as:
         *
         * Version: "1"
         * Address type and address:
         *   "1" + an IPv4 address as 8 hex digits
         *   "2" + a DECNET address as 12 hex digits
         *   "6" + an IPv6 address as 32 hex digits
         * Time stamp: milliseconds since UNIX epoch as 13 decimal digits
         * Process-ID type and process-ID:
         *   "1" + POSIX PID as 10 decimal digits
         * Sequence number as 4 decimal digits
         *
         * XSMP client IDs are supposed to be globally unique: if
         * SmsGenerateClientID() is unable to determine a network
         * address for the machine, it gives up and returns %NULL.
         * GNOME and KDE have traditionally used a fourth address
         * format in this case:
         *   "0" + 16 random hex digits
         *
         * We don't even bother trying SmsGenerateClientID(), since the
         * user's IP address is probably "192.168.1.*" anyway, so a random
         * number is actually more likely to be globally unique.
         */

        if (!rand1) {
                rand1 = g_random_int ();
                rand2 = g_random_int ();
                pid = getpid ();
        }

        sequence = (sequence + 1) % 10000;
        gettimeofday (&tv, NULL);
        return g_strdup_printf ("10%.04x%.04x%.10lu%.3u%.10lu%.4d",
                                rand1,
                                rand2,
                                (unsigned long) tv.tv_sec,
                                (unsigned) tv.tv_usec,
                                (unsigned long) pid,
                                sequence);
}

static gboolean
gsm_util_update_activation_environment (const char  *variable,
                                        const char  *value,
                                        GError     **error)
{
        GDBusConnection *connection;
        gboolean         environment_updated;
        GVariantBuilder  builder;
        GVariant        *reply;
        GError          *bus_error = NULL;

        environment_updated = FALSE;
        connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, error);

        if (connection == NULL) {
                return FALSE;
        }

        g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ss}"));
        g_variant_builder_add (&builder, "{ss}", variable, value);

        reply = g_dbus_connection_call_sync (connection,
                                             "org.freedesktop.DBus",
                                             "/org/freedesktop/DBus",
                                             "org.freedesktop.DBus",
                                             "UpdateActivationEnvironment",
                                             g_variant_new ("(@a{ss})",
                                                            g_variant_builder_end (&builder)),
                                             NULL,
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1, NULL, &bus_error);

        if (bus_error != NULL) {
                g_propagate_error (error, bus_error);
        } else {
                environment_updated = TRUE;
                g_variant_unref (reply);
        }

        g_clear_object (&connection);

        return environment_updated;
}

void
gsm_util_setenv (const char *variable,
                 const char *value)
{
        GError *bus_error;

        if (child_environment == NULL)
                child_environment = g_listenv ();

        if (!value)
                child_environment = g_environ_unsetenv (child_environment, variable);
        else
                child_environment = g_environ_setenv (child_environment, variable, value, TRUE);

        bus_error = NULL;

        /* If this fails it isn't fatal, it means some things like session
         * management and keyring won't work in activated clients.
         */
        if (!gsm_util_update_activation_environment (variable, value, &bus_error)) {
                g_warning ("Could not make bus activated clients aware of %s=%s environment variable: %s", variable, value, bus_error->message);
                g_error_free (bus_error);
        }
}

const char * const *
gsm_util_listenv (void)
{
        return (const char * const *) child_environment;

}

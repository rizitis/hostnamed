/*
 * Copyright (C) 2023 Andrey V.Kosteltsev <kx@radix.pro>
 *
 * Licensed under the GNU General Public License Version 2
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include <gio/gio.h>
#include <glib.h>
#include <glib-unix.h>
#include <glib-object.h>
#include <glib/gi18n-lib.h>
#include <locale.h>

#include "rcl-hostname.h"

#define HOSTNAME_SERVICE_NAME "org.freedesktop.hostname1"

/* ───────────────────────────────────────────── */

typedef struct RclState
{
  RclDaemon  *daemon;
  GMainLoop   *loop;

  gboolean     debug;
  gboolean     verbose;
} RclState;

/* ───────────────────────────────────────────── */

static void
rcl_state_free(RclState *state)
{
  rcl_daemon_shutdown(state->daemon);
  g_clear_object(&state->daemon);
  g_clear_pointer(&state->loop, g_main_loop_unref);
  g_free(state);
}

static RclState *
rcl_state_new(void)
{
  RclState *state = g_new0(RclState, 1);

  state->daemon = rcl_daemon_new();
  state->loop   = g_main_loop_new(NULL, FALSE);

  return state;
}

/* ───────────────────────────────────────────── */

#define LOG(state, fmt, ...) \
  if ((state)->debug || (state)->verbose) \
    g_debug("[hostnamed] " fmt, ##__VA_ARGS__)

/* ───────────────────────────────────────────── */

static void
rcl_main_bus_acquired(GDBusConnection *connection,
                      const gchar     *name,
                      gpointer         user_data)
{
  RclState *state = user_data;

  LOG(state, "bus acquired");

  if (!rcl_daemon_startup(state->daemon, connection))
  {
    g_warning("Could not startup daemon");
    g_main_loop_quit(state->loop);
    return;
  }

  LOG(state, "daemon startup complete");
}

/* ───────────────────────────────────────────── */

static void
rcl_main_name_acquired(GDBusConnection *connection,
                       const gchar     *name,
                       gpointer         user_data)
{
  RclState *state = user_data;
  LOG(state, "name acquired: %s", name);
}

/* ───────────────────────────────────────────── */

static void
rcl_main_name_lost(GDBusConnection *connection,
                   const gchar     *name,
                   gpointer         user_data)
{
  RclState *state = user_data;

  g_warning("D-Bus name lost: %s", name);
  LOG(state, "name lost: %s", name);

  /* DO NOT auto-exit unless you really want to crash loop */
}

/* ───────────────────────────────────────────── */

static gboolean
rcl_main_sigint_cb(gpointer user_data)
{
  RclState *state = user_data;
  LOG(state, "SIGINT received");
  g_main_loop_quit(state->loop);
  return FALSE;
}

static gboolean
rcl_main_sigterm_cb(gpointer user_data)
{
  RclState *state = user_data;
  LOG(state, "SIGTERM received");
  g_main_loop_quit(state->loop);
  return FALSE;
}

/* ───────────────────────────────────────────── */

static void
rcl_main_log_handler_cb(const gchar    *log_domain,
                        GLogLevelFlags  log_level,
                        const gchar    *message,
                        gpointer        user_data)
{
  gchar timebuf[32];
  time_t t;

  time(&t);
  strftime(timebuf, sizeof(timebuf), "%H:%M:%S", localtime(&t));

  const char *color = (log_level >= G_LOG_LEVEL_WARNING) ? "\033[31m" : "\033[34m";

  g_print("\033[32mTI:%s\t%s%s\033[0m\n", timebuf, color, message);
}

/* ───────────────────────────────────────────── */

static gboolean
rcl_main_inotify_cb(gpointer user_data)
{
  RclState *state = user_data;
  RclHostnameDaemon *object = RCL_HOSTNAME_DAEMON(state->daemon);

  LOG(state, "file change detected → syncing DBus properties");

  rcl_daemon_sync_dbus_properties(object);

  return G_SOURCE_CONTINUE;
}

/* ───────────────────────────────────────────── */

gint main(gint argc, gchar **argv)
{
  GError         *error = NULL;
  GOptionContext *context;

  gboolean debug   = FALSE;
  gboolean verbose = FALSE;
  gboolean replace = FALSE;

  RclState *state;

  guint watch_id;
  GBusNameOwnerFlags bus_flags;

  GFile        *hostname_file;
  GFile        *machineinfo_file;
  GFileMonitor *hostname_mon;
  GFileMonitor *machineinfo_mon;

  const GOptionEntry options[] =
  {
    { "replace", 'r', 0, G_OPTION_ARG_NONE, &replace, "Replace existing daemon", NULL },
    { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose, "Verbose output", NULL },
    { "debug",   'd', 0, G_OPTION_ARG_NONE, &debug,   "Debug output (implies verbose)", NULL },
    { NULL }
  };

  setlocale(LC_ALL, "");

  context = g_option_context_new("");
  g_option_context_add_main_entries(context, options, NULL);

  if (!g_option_context_parse(context, &argc, &argv, &error))
  {
    g_warning("arg parse failed: %s", error->message);
    return 1;
  }

  if (debug)
    verbose = TRUE;

  state = rcl_state_new();
  state->debug = debug;
  state->verbose = verbose;

  rcl_daemon_set_debug(state->daemon, debug);

  /* signals */
  g_unix_signal_add(SIGINT,  rcl_main_sigint_cb, state);
  g_unix_signal_add(SIGTERM, rcl_main_sigterm_cb, state);

  /* logging */
  if (verbose)
  {
    g_log_set_handler(NULL,
                      G_LOG_LEVEL_MASK,
                      rcl_main_log_handler_cb,
                      NULL);
  }

  /* file monitoring */
  hostname_file = g_file_new_for_path("/etc/HOSTNAME");
  hostname_mon = g_file_monitor_file(hostname_file,
                                     G_FILE_MONITOR_WATCH_MOVES,
                                     NULL, &error);

  if (hostname_mon)
    g_signal_connect_swapped(hostname_mon, "changed",
                             G_CALLBACK(rcl_main_inotify_cb), state);

  machineinfo_file = g_file_new_for_path("/etc/machine-info");
  machineinfo_mon = g_file_monitor_file(machineinfo_file,
                                        G_FILE_MONITOR_WATCH_MOVES,
                                        NULL, &error);

  if (machineinfo_mon)
    g_signal_connect_swapped(machineinfo_mon, "changed",
                             G_CALLBACK(rcl_main_inotify_cb), state);

  /* DBus ownership */
  bus_flags = G_BUS_NAME_OWNER_FLAGS_NONE;
  if (replace)
    bus_flags |= G_BUS_NAME_OWNER_FLAGS_REPLACE;

  watch_id = g_bus_own_name(G_BUS_TYPE_SYSTEM,
                            HOSTNAME_SERVICE_NAME,
                            bus_flags,
                            rcl_main_bus_acquired,
                            rcl_main_name_acquired,
                            rcl_main_name_lost,
                            state,
                            NULL);

  g_debug("hostnamed started (%s)", PACKAGE_VERSION);

  g_main_loop_run(state->loop);

  g_bus_unown_name(watch_id);

  g_clear_object(&hostname_mon);
  g_clear_object(&machineinfo_mon);
  g_clear_object(&hostname_file);
  g_clear_object(&machineinfo_file);

  rcl_state_free(state);

  return 0;
}

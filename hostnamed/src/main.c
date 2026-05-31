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

typedef struct RclState
{
  RclDaemon  *daemon;
  GMainLoop  *loop;
} RclState;

static void
rcl_state_free( RclState *state )
{
  rcl_daemon_shutdown( state->daemon );

  g_clear_object( &state->daemon );
  g_clear_pointer( &state->loop, g_main_loop_unref );

  g_free( state );
}

static RclState *
rcl_state_new( void )
{
  RclState *state = g_new0( RclState, 1 );

  state->daemon = rcl_daemon_new();
  state->loop = g_main_loop_new( NULL, FALSE );

  return state;
}

/************************
  rcl_main_bus_acquired:
 */
static void
rcl_main_bus_acquired( GDBusConnection *connection,
                       const gchar     *name,
                       gpointer         user_data )
{
  RclState *state = user_data;

  if( !rcl_daemon_startup( state->daemon, connection ) )
  {
    g_warning( "Could not startup daemon" );
    g_main_loop_quit( state->loop );
  }
}

/*************************
  rcl_main_name_acquired:
 */
static void
rcl_main_name_acquired( GDBusConnection *connection,
                        const gchar     *name,
                        gpointer         user_data )
{
  g_debug( "Acquired the name %s", name );
}

/*********************
  rcl_main_name_lost:
 */
static void
rcl_main_name_lost( GDBusConnection *connection,
                    const gchar     *name,
                    gpointer         user_data )
{
  RclState *state = user_data;
  g_debug( "Name lost, exiting" );
  g_main_loop_quit( state->loop );
}

/*********************
  rcl_main_sigint_cb:
 */
static gboolean
rcl_main_sigint_cb( gpointer user_data )
{
  RclState *state = user_data;
  g_debug( "Handling SIGINT" );
  g_main_loop_quit( state->loop );
  return FALSE;
}

static gboolean
rcl_main_sigterm_cb( gpointer user_data )
{
  RclState *state = user_data;
  g_debug( "Handling SIGTERM" );
  g_main_loop_quit( state->loop );
  return FALSE;
}

/*************************
  rcl_main_log_ignore_cb:
 */
static void
rcl_main_log_ignore_cb( const gchar    *log_domain,
                        GLogLevelFlags  log_level,
                        const gchar    *message,
                        gpointer        user_data )
{
}

/**************************
  rcl_main_log_handler_cb:
 */
static void
rcl_main_log_handler_cb( const gchar    *log_domain,
                         GLogLevelFlags  log_level,
                         const gchar    *message,
                         gpointer        user_data )
{
  gchar str_time[255];
  time_t the_time;

  /* header always in green */
  time( &the_time );
  strftime( str_time, 254, "%H:%M:%S", localtime( &the_time ) );
  g_print( "%c[%dmTI:%s\t", 0x1B, 32, str_time );

  /* critical is also in red */
  if( log_level == G_LOG_LEVEL_CRITICAL ||
      log_level == G_LOG_LEVEL_WARNING  ||
      log_level == G_LOG_LEVEL_ERROR )
  {
    g_print( "%c[%dm%s\n%c[%dm", 0x1B, 31, message, 0x1B, 0 );
  }
  else
  {
    /* debug in blue */
    g_print( "%c[%dm%s\n%c[%dm", 0x1B, 34, message, 0x1B, 0 );
  }
}

/***********************
  rcl_main_inotify_cb:

  Called when inotify detects a change to /etc/HOSTNAME or /etc/machine-info.
  Re-reads the files and refreshes D-Bus properties. Unlike timedated's polling
  timer, hostname data only changes when those files are written, so inotify is
  the right mechanism here.
 */
static gboolean
rcl_main_inotify_cb( gpointer user_data )
{
  RclState *state = (RclState *)user_data;
  RclHostnameDaemon *object = RCL_HOSTNAME_DAEMON( state->daemon );

  g_debug( "File change detected – refreshing hostname properties" );
  rcl_daemon_sync_dbus_properties( object );

  return G_SOURCE_CONTINUE;
}

/*******
  main:
 */
gint main( gint argc, gchar **argv )
{
  GError             *error    = NULL;
  GOptionContext     *context;
  guint               watch_id = 0;
  gboolean            debug    = FALSE;
  gboolean            verbose  = FALSE;
  RclState           *state;
  GBusNameOwnerFlags  bus_flags;
  gboolean            replace  = FALSE;

  /*
   * inotify monitors for /etc/HOSTNAME and /etc/machine-info.
   * We create one GFileMonitor per file and connect both to the
   * same callback so any write triggers a property refresh.
   */
  GFile        *hostname_file    = NULL;
  GFile        *machineinfo_file = NULL;
  GFileMonitor *hostname_mon     = NULL;
  GFileMonitor *machineinfo_mon  = NULL;

  const GOptionEntry options[] = {
    { "replace", 'r', 0, G_OPTION_ARG_NONE, &replace, _("Replace the old daemon"),               NULL },
    { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose, _("Show extra debugging information"),     NULL },
    { "debug",   'd', 0, G_OPTION_ARG_NONE, &debug,   _("Enable debugging (implies --verbose)"), NULL },
    { NULL }
  };

#if !defined(GLIB_VERSION_2_36)
  g_type_init();
#endif
  setlocale( LC_ALL, "" );

  /* NOTE: do not use bind_textdomain() when using gi18n-lib.h */
  bind_textdomain_codeset( GETTEXT_PACKAGE, "UTF-8" );
  textdomain( GETTEXT_PACKAGE );

  context = g_option_context_new( "" );
  g_option_context_add_main_entries( context, options, NULL );
  if( !g_option_context_parse( context, &argc, &argv, &error ) )
  {
    g_warning( "Failed to parse command-line options: %s", error->message );
    g_error_free( error );
    return 1;
  }
  g_option_context_free( context );

  if( debug )
    verbose = TRUE;

  /* verbose? */
  if( verbose )
  {
    g_log_set_fatal_mask( NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL );
    g_log_set_handler( G_LOG_DOMAIN,
                       G_LOG_LEVEL_ERROR    |
                       G_LOG_LEVEL_CRITICAL |
                       G_LOG_LEVEL_DEBUG    |
                       G_LOG_LEVEL_WARNING,
                       rcl_main_log_handler_cb, NULL );
    g_log_set_handler( "Hostname-Linux",
                       G_LOG_LEVEL_ERROR    |
                       G_LOG_LEVEL_CRITICAL |
                       G_LOG_LEVEL_DEBUG    |
                       G_LOG_LEVEL_WARNING,
                       rcl_main_log_handler_cb, NULL );
  }
  else
  {
    /* hide all debugging */
    g_log_set_fatal_mask( NULL, G_LOG_LEVEL_ERROR );
    g_log_set_handler( G_LOG_DOMAIN,
                       G_LOG_LEVEL_DEBUG,
                       rcl_main_log_ignore_cb,
                       NULL );
    g_log_set_handler( "Hostname-Linux",
                       G_LOG_LEVEL_DEBUG,
                       rcl_main_log_ignore_cb,
                       NULL );
  }

  /* initialize state */
  state = rcl_state_new();
  rcl_daemon_set_debug( state->daemon, debug );

  /* do stuff on ctrl-c */
  g_unix_signal_add_full( G_PRIORITY_DEFAULT,
                          SIGINT,
                          rcl_main_sigint_cb,
                          state,
                          NULL );

  /* clean shutdown on SIGTERM */
  g_unix_signal_add_full( G_PRIORITY_DEFAULT,
                          SIGTERM,
                          rcl_main_sigterm_cb,
                          state,
                          NULL );

  /*
   * Watch /etc/HOSTNAME and /etc/machine-info with inotify via GFileMonitor.
   *
   * Unlike timedated where a 1-second poll is appropriate (the clock always
   * ticks), hostname data is static between explicit changes. Watching the
   * files directly means we react immediately to external edits (e.g. from
   * another tool writing /etc/HOSTNAME) while burning zero CPU at idle.
   *
   * G_FILE_MONITOR_WATCH_MOVES also catches atomic rename-based writes that
   * editors and systemd-hostnamed itself use (write to temp file, rename).
   */
  hostname_file = g_file_new_for_path( "/etc/HOSTNAME" );
  hostname_mon  = g_file_monitor_file( hostname_file,
                                       G_FILE_MONITOR_WATCH_MOVES,
                                       NULL, &error );
  if( hostname_mon )
  {
    g_signal_connect_swapped( hostname_mon, "changed",
                              G_CALLBACK( rcl_main_inotify_cb ), state );
    g_debug( "Watching /etc/HOSTNAME for changes" );
  }
  else
  {
    g_warning( "Could not watch /etc/HOSTNAME: %s", error->message );
    g_clear_error( &error );
  }

  machineinfo_file = g_file_new_for_path( "/etc/machine-info" );
  machineinfo_mon  = g_file_monitor_file( machineinfo_file,
                                          G_FILE_MONITOR_WATCH_MOVES,
                                          NULL, &error );
  if( machineinfo_mon )
  {
    g_signal_connect_swapped( machineinfo_mon, "changed",
                              G_CALLBACK( rcl_main_inotify_cb ), state );
    g_debug( "Watching /etc/machine-info for changes" );
  }
  else
  {
    g_warning( "Could not watch /etc/machine-info: %s", error->message );
    g_clear_error( &error );
  }

  /* acquire name */
  bus_flags = G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT;
  if( replace )
    bus_flags |= G_BUS_NAME_OWNER_FLAGS_REPLACE;

  watch_id = g_bus_own_name( G_BUS_TYPE_SYSTEM,
                             HOSTNAME_SERVICE_NAME,
                             bus_flags,
                             rcl_main_bus_acquired,
                             rcl_main_name_acquired,
                             rcl_main_name_lost,
                             state, NULL );

  g_debug( "Starting hostnamed version %s", PACKAGE_VERSION );

  /* wait for signals or file changes */
  g_main_loop_run( state->loop );

  /* cleanup */
  g_bus_unown_name( watch_id );

  if( hostname_mon )
  {
    g_file_monitor_cancel( hostname_mon );
    g_object_unref( hostname_mon );
  }
  if( machineinfo_mon )
  {
    g_file_monitor_cancel( machineinfo_mon );
    g_object_unref( machineinfo_mon );
  }

  g_object_unref( hostname_file );
  g_object_unref( machineinfo_file );

  rcl_state_free( state );

  return 0;
}

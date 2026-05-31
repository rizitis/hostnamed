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

#include <errno.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#include <glib/gi18n-lib.h>
#include <polkit/polkit.h>

#include "rcl-hostname.h"

/* --------------------------------------------------------------------------
   The org.freedesktop.hostname1 introspection XML.

   Generated once at startup by g_dbus_node_info_new_for_xml() and kept for
   the lifetime of the process.  All method/property names here must match
   the handler and getter/setter functions below.
   -------------------------------------------------------------------------- */
static const gchar hostname1_introspection_xml[] =
  "<node>"
  "  <interface name='org.freedesktop.hostname1'>"
  /* ---- read-only properties ---- */
  "    <property name='Hostname'                    type='s' access='read'/>"
  "    <property name='StaticHostname'              type='s' access='read'/>"
  "    <property name='PrettyHostname'              type='s' access='read'/>"
  "    <property name='DefaultHostname'             type='s' access='read'/>"
  "    <property name='HostnameSource'              type='s' access='read'/>"
  "    <property name='IconName'                    type='s' access='read'/>"
  "    <property name='Chassis'                     type='s' access='read'/>"
  "    <property name='Deployment'                  type='s' access='read'/>"
  "    <property name='Location'                    type='s' access='read'/>"
  "    <property name='KernelName'                  type='s' access='read'/>"
  "    <property name='KernelRelease'               type='s' access='read'/>"
  "    <property name='KernelVersion'               type='s' access='read'/>"
  "    <property name='OperatingSystemPrettyName'   type='s' access='read'/>"
  "    <property name='OperatingSystemCPEName'      type='s' access='read'/>"
  "    <property name='OperatingSystemHomeURL'      type='s' access='read'/>"
  "    <property name='HardwareVendor'              type='s' access='read'/>"
  "    <property name='HardwareModel'               type='s' access='read'/>"
  "    <property name='FirmwareVersion'             type='s' access='read'/>"
  "    <property name='FirmwareDate'                type='s' access='read'/>"
  /* ---- methods ---- */
  "    <method name='SetHostname'>"
  "      <arg name='name'        type='s' direction='in'/>"
  "      <arg name='interactive' type='b' direction='in'/>"
  "    </method>"
  "    <method name='SetStaticHostname'>"
  "      <arg name='name'        type='s' direction='in'/>"
  "      <arg name='interactive' type='b' direction='in'/>"
  "    </method>"
  "    <method name='SetPrettyHostname'>"
  "      <arg name='name'        type='s' direction='in'/>"
  "      <arg name='interactive' type='b' direction='in'/>"
  "    </method>"
  "    <method name='SetIconName'>"
  "      <arg name='name'        type='s' direction='in'/>"
  "      <arg name='interactive' type='b' direction='in'/>"
  "    </method>"
  "    <method name='SetChassis'>"
  "      <arg name='name'        type='s' direction='in'/>"
  "      <arg name='interactive' type='b' direction='in'/>"
  "    </method>"
  "    <method name='SetDeployment'>"
  "      <arg name='name'        type='s' direction='in'/>"
  "      <arg name='interactive' type='b' direction='in'/>"
  "    </method>"
  "    <method name='SetLocation'>"
  "      <arg name='name'        type='s' direction='in'/>"
  "      <arg name='interactive' type='b' direction='in'/>"
  "    </method>"
  "    <method name='GetProductUUID'>"
  "      <arg name='interactive' type='b' direction='in'/>"
  "      <arg name='uuid'        type='ay' direction='out'/>"
  "    </method>"
  "    <method name='GetHardwareSerial'>"
  "      <arg name='interactive' type='b' direction='in'/>"
  "      <arg name='serial'      type='s' direction='out'/>"
  "    </method>"
  "    <method name='Describe'>"
  "      <arg name='json' type='s' direction='out'/>"
  "    </method>"
  "  </interface>"
  "</node>";

/* --------------------------------------------------------------------------
   Private data
   -------------------------------------------------------------------------- */
struct _RclHostnameDaemonPrivate
{
  GDBusConnection  *connection;
  GDBusNodeInfo    *introspection;
  guint             registration_id;
  gboolean          debug;

  /* Cached property values.  Refreshed by rcl_daemon_sync_dbus_properties(). */
  gchar *hostname;
  gchar *static_hostname;
  gchar *pretty_hostname;
  gchar *default_hostname;
  gchar *hostname_source;
  gchar *icon_name;
  gchar *chassis;
  gchar *deployment;
  gchar *location;
  gchar *kernel_name;
  gchar *kernel_release;
  gchar *kernel_version;
  gchar *os_pretty_name;
  gchar *os_cpe_name;
  gchar *os_home_url;
  gchar *hw_vendor;
  gchar *hw_model;
  gchar *firmware_version;
  gchar *firmware_date;
};

/* --------------------------------------------------------------------------
   GObject boilerplate
   -------------------------------------------------------------------------- */
G_DEFINE_TYPE_WITH_PRIVATE( RclHostnameDaemon, rcl_hostname_daemon, RCL_TYPE_DAEMON )

/* Also define the abstract RclDaemon base just enough that GObject is happy.
   In a real multi-daemon codebase this would live in its own rcl-daemon.c. */
G_DEFINE_TYPE( RclDaemon, rcl_daemon, G_TYPE_OBJECT )

static void rcl_daemon_class_init( RclDaemonClass *klass ) {}
static void rcl_daemon_init( RclDaemon *self ) {}

/* --------------------------------------------------------------------------
   Chassis helpers
   -------------------------------------------------------------------------- */

/*
 * SMBIOS chassis types → freedesktop icon names.
 * Numeric values from SMBIOS spec 3.x, Table 17.
 */
const gchar *
rcl_chassis_type_to_icon_name( guint t )
{
  switch( t )
  {
    case  3: /* Desktop          */ return "computer-desktop";
    case  4: /* Low Profile      */ return "computer-desktop";
    case  6: /* Mini Tower       */ return "computer-desktop";
    case  7: /* Tower            */ return "computer-desktop";
    case  8: /* Portable         */ return "computer-laptop";
    case  9: /* Laptop           */ return "computer-laptop";
    case 10: /* Notebook         */ return "computer-laptop";
    case 11: /* Hand-held        */ return "phone";
    case 14: /* Sub-Notebook     */ return "computer-laptop";
    case 18: /* Expansion Chassis*/ return "computer-desktop";
    case 21: /* Peripheral       */ return "computer-desktop";
    case 30: /* Tablet           */ return "computer-tablet";
    case 31: /* Convertible      */ return "computer-laptop";
    case 32: /* Detachable       */ return "computer-tablet";
    default:                        return "computer";
  }
}

const gchar *
rcl_chassis_type_to_string( guint t )
{
  switch( t )
  {
    case  3: case  4: case  6: case  7: return "desktop";
    case  8: case  9: case 10: case 14: return "laptop";
    case 11:                            return "handset";
    case 17:                            return "server";
    case 30: case 32:                   return "tablet";
    case 31:                            return "convertible";
    default:                            return "";
  }
}

/* --------------------------------------------------------------------------
   Small file-reading helpers
   -------------------------------------------------------------------------- */

/* Read a single-line text file (e.g. /etc/hostname), strip trailing newline.
   Returns a newly-allocated string, or NULL on error. */
static gchar *
read_first_line( const gchar *path )
{
  gchar *contents = NULL;
  gchar *nl;

  if( !g_file_get_contents( path, &contents, NULL, NULL ) )
    return NULL;

  nl = strchr( contents, '\n' );
  if( nl ) *nl = '\0';

  g_strstrip( contents );
  return contents;
}

/* Read a KEY=value file (like /etc/os-release or /etc/machine-info) and
   return the unquoted value for 'key', or NULL if not found. */
static gchar *
read_key_value_file( const gchar *path, const gchar *key )
{
  gchar  *contents = NULL;
  gchar **lines;
  gchar  *result   = NULL;
  gsize   i;
  gchar   prefix[256];

  if( !g_file_get_contents( path, &contents, NULL, NULL ) )
    return NULL;

  g_snprintf( prefix, sizeof(prefix), "%s=", key );
  lines = g_strsplit( contents, "\n", -1 );

  for( i = 0; lines[i] != NULL; i++ )
  {
    if( g_str_has_prefix( lines[i], prefix ) )
    {
      gchar *val = lines[i] + strlen( prefix );

      /* Strip surrounding double-quotes if present */
      if( val[0] == '"' )
      {
        gsize len = strlen( val );
        if( len >= 2 && val[len-1] == '"' )
        {
          result = g_strndup( val + 1, len - 2 );
        }
        else
        {
          result = g_strdup( val + 1 );
        }
      }
      else
      {
        result = g_strdup( val );
      }
      break;
    }
  }

  g_strfreev( lines );
  g_free( contents );
  return result;
}

/* Read a DMI sysfs file, stripping whitespace. Returns "" on error so
   callers never get NULL for DMI fields. */
static gchar *
read_dmi( const gchar *path )
{
  gchar *val = read_first_line( path );
  return val ? val : g_strdup( "" );
}

/* --------------------------------------------------------------------------
   Property refresh
   -------------------------------------------------------------------------- */

/*
 * Populate (or refresh) all cached properties from the underlying sources,
 * then emit PropertiesChanged for anything that actually changed.
 */
void
rcl_daemon_sync_dbus_properties( RclHostnameDaemon *daemon )
{
  RclHostnameDaemonPrivate *priv = daemon->priv;
  struct utsname uts;
  guint chassis_num = 0;
  gchar *tmp;

  GVariantBuilder changed_props;
  gboolean any_changed = FALSE;

  g_variant_builder_init( &changed_props, G_VARIANT_TYPE("a{sv}") );

#define UPDATE_STR_PROP( field, new_val, dbus_name )              \
  do {                                                            \
    gchar *_nv = (new_val);                                       \
    if( g_strcmp0( priv->field, _nv ) != 0 )                     \
    {                                                             \
      g_free( priv->field );                                      \
      priv->field = _nv ? g_strdup(_nv) : g_strdup("");          \
      g_variant_builder_add( &changed_props, "{sv}",             \
                             dbus_name,                           \
                             g_variant_new_string(priv->field) );\
      any_changed = TRUE;                                         \
    }                                                             \
    g_free( _nv );                                                \
  } while(0)

  /* ---- transient hostname ---- */
  {
    gchar buf[256] = {0};
    if( gethostname( buf, sizeof(buf) - 1 ) == 0 )
      tmp = g_strdup( buf );
    else
      tmp = g_strdup( "" );
    UPDATE_STR_PROP( hostname, tmp, "Hostname" );
  }

  /* ---- static hostname ---- */
  UPDATE_STR_PROP( static_hostname,
                   read_first_line( RCL_HOSTNAME_FILE ),
                   "StaticHostname" );

  /* ---- /etc/machine-info fields ---- */
  UPDATE_STR_PROP( pretty_hostname,
                   read_key_value_file( RCL_MACHINE_INFO_FILE, "PRETTY_HOSTNAME" ),
                   "PrettyHostname" );

  UPDATE_STR_PROP( icon_name,
                   read_key_value_file( RCL_MACHINE_INFO_FILE, "ICON_NAME" ),
                   "IconName" );

  UPDATE_STR_PROP( chassis,
                   read_key_value_file( RCL_MACHINE_INFO_FILE, "CHASSIS" ),
                   "Chassis" );

  UPDATE_STR_PROP( deployment,
                   read_key_value_file( RCL_MACHINE_INFO_FILE, "DEPLOYMENT" ),
                   "Deployment" );

  UPDATE_STR_PROP( location,
                   read_key_value_file( RCL_MACHINE_INFO_FILE, "LOCATION" ),
                   "Location" );

  /* ---- fall back chassis / icon from DMI if not set in machine-info ---- */
  if( priv->chassis == NULL || priv->chassis[0] == '\0' )
  {
    gchar *dmi_chassis_type = read_dmi( RCL_DMI_CHASSIS_TYPE );
    chassis_num = (guint) g_ascii_strtoull( dmi_chassis_type, NULL, 10 );
    g_free( dmi_chassis_type );

    g_free( priv->chassis );
    priv->chassis = g_strdup( rcl_chassis_type_to_string( chassis_num ) );

    g_variant_builder_add( &changed_props, "{sv}", "Chassis",
                           g_variant_new_string( priv->chassis ) );
    any_changed = TRUE;
  }

  if( priv->icon_name == NULL || priv->icon_name[0] == '\0' )
  {
    g_free( priv->icon_name );
    priv->icon_name = g_strdup( rcl_chassis_type_to_icon_name( chassis_num ) );

    g_variant_builder_add( &changed_props, "{sv}", "IconName",
                           g_variant_new_string( priv->icon_name ) );
    any_changed = TRUE;
  }

  /* ---- HostnameSource ---- */
  {
    const gchar *src;
    if( priv->static_hostname && priv->static_hostname[0] != '\0' )
      src = "static";
    else if( priv->hostname && priv->hostname[0] != '\0' )
      src = "transient";
    else
      src = "default";
    UPDATE_STR_PROP( hostname_source, g_strdup(src), "HostnameSource" );
  }

  /* ---- kernel info from uname(2) ---- */
  if( uname( &uts ) == 0 )
  {
    UPDATE_STR_PROP( kernel_name,    g_strdup(uts.sysname),  "KernelName" );
    UPDATE_STR_PROP( kernel_release, g_strdup(uts.release),  "KernelRelease" );
    UPDATE_STR_PROP( kernel_version, g_strdup(uts.version),  "KernelVersion" );
  }

  /* ---- /etc/os-release ---- */
  UPDATE_STR_PROP( os_pretty_name,
                   read_key_value_file( RCL_OS_RELEASE_FILE, "PRETTY_NAME" ),
                   "OperatingSystemPrettyName" );

  UPDATE_STR_PROP( os_cpe_name,
                   read_key_value_file( RCL_OS_RELEASE_FILE, "CPE_NAME" ),
                   "OperatingSystemCPEName" );

  UPDATE_STR_PROP( os_home_url,
                   read_key_value_file( RCL_OS_RELEASE_FILE, "HOME_URL" ),
                   "OperatingSystemHomeURL" );

  /* ---- DMI hardware info ---- */
  UPDATE_STR_PROP( hw_vendor,        read_dmi(RCL_DMI_BOARD_VENDOR),  "HardwareVendor" );
  UPDATE_STR_PROP( hw_model,         read_dmi(RCL_DMI_PRODUCT_NAME),  "HardwareModel" );
  UPDATE_STR_PROP( firmware_version, read_dmi(RCL_DMI_BIOS_VERSION),  "FirmwareVersion" );
  UPDATE_STR_PROP( firmware_date,    read_dmi(RCL_DMI_BIOS_DATE),     "FirmwareDate" );

#undef UPDATE_STR_PROP

  /* Emit PropertiesChanged only if something actually changed */
  if( any_changed && priv->connection && priv->registration_id > 0 )
  {
    GError *err = NULL;
    g_dbus_connection_emit_signal(
      priv->connection,
      NULL,
      "/org/freedesktop/hostname1",
      "org.freedesktop.DBus.Properties",
      "PropertiesChanged",
      g_variant_new( "(sa{sv}as)",
                     "org.freedesktop.hostname1",
                     &changed_props,
                     NULL ),
      &err );

    if( err )
    {
      g_warning( "PropertiesChanged emission failed: %s", err->message );
      g_error_free( err );
    }
  }
  else
  {
    g_variant_builder_clear( &changed_props );
  }
}

/* --------------------------------------------------------------------------
   machine-info writer

   All Set* methods that touch /etc/machine-info funnel through here.
   The file is rewritten atomically (write to temp, rename).
   -------------------------------------------------------------------------- */
static gboolean
write_machine_info_key( const gchar  *key,
                        const gchar  *value,
                        GError      **error )
{
  gchar  *contents  = NULL;
  gchar **lines;
  gsize   i;
  GString *out;
  gboolean found = FALSE;
  gchar   *tmp_path;
  gboolean ok;

  /* Load existing file, tolerating absence */
  g_file_get_contents( RCL_MACHINE_INFO_FILE, &contents, NULL, NULL );
  lines = contents ? g_strsplit( contents, "\n", -1 )
                   : g_new0( gchar *, 1 );
  g_free( contents );

  out = g_string_new( NULL );
  gchar prefix[256];
  g_snprintf( prefix, sizeof(prefix), "%s=", key );

  for( i = 0; lines[i] != NULL; i++ )
  {
    if( lines[i][0] == '\0' ) continue; /* skip blank lines; we'll add a trailing newline */

    if( g_str_has_prefix( lines[i], prefix ) )
    {
      /* Replace or remove this key */
      if( value && value[0] != '\0' )
        g_string_append_printf( out, "%s=\"%s\"\n", key, value );
      found = TRUE;
    }
    else
    {
      g_string_append_printf( out, "%s\n", lines[i] );
    }
  }

  /* Key wasn't present yet – append it */
  if( !found && value && value[0] != '\0' )
    g_string_append_printf( out, "%s=\"%s\"\n", key, value );

  g_strfreev( lines );

  /* Write atomically */
  tmp_path = g_strdup( RCL_MACHINE_INFO_FILE ".tmp" );
  ok = g_file_set_contents( tmp_path, out->str, (gssize)out->len, error );

  if( ok )
  {
    if( g_rename( tmp_path, RCL_MACHINE_INFO_FILE ) != 0 )
    {
      g_set_error( error, G_IO_ERROR, g_io_error_from_errno(errno),
                   "rename(%s, %s): %s", tmp_path,
                   RCL_MACHINE_INFO_FILE, g_strerror(errno) );
      ok = FALSE;
    }
  }

  g_free( tmp_path );
  g_string_free( out, TRUE );
  return ok;
}

/* --------------------------------------------------------------------------
   Polkit authorisation helper
   -------------------------------------------------------------------------- */
static gboolean
check_polkit( GDBusMethodInvocation *invocation,
              const gchar           *action_id,
              gboolean               interactive,
              GError               **error )
{
  PolkitAuthority *authority = NULL;
  PolkitSubject   *subject   = NULL;
  PolkitAuthorizationResult *result = NULL;
  PolkitCheckAuthorizationFlags flags;
  gboolean authorised = FALSE;
  const gchar *sender;
  GError *local_error = NULL;

  sender = g_dbus_method_invocation_get_sender( invocation );

  authority = polkit_authority_get_sync( NULL, &local_error );
  if( !authority )
  {
    g_propagate_error( error, local_error );
    return FALSE;
  }

  subject = polkit_system_bus_name_new( sender );
  flags   = interactive
              ? POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION
              : POLKIT_CHECK_AUTHORIZATION_FLAGS_NONE;

  result = polkit_authority_check_authorization_sync(
    authority, subject, action_id, NULL, flags, NULL, &local_error );

  if( local_error )
  {
    g_propagate_error( error, local_error );
  }
  else
  {
    authorised = polkit_authorization_result_get_is_authorized( result ) ||
                 polkit_authorization_result_get_is_challenge( result );
    if( !authorised )
      g_set_error( error, G_DBUS_ERROR, G_DBUS_ERROR_AUTH_FAILED,
                   "Not authorised to perform action '%s'", action_id );
  }

  g_clear_object( &result );
  g_clear_object( &subject );
  g_clear_object( &authority );

  return authorised;
}

/* --------------------------------------------------------------------------
   D-Bus property getter
   -------------------------------------------------------------------------- */
static GVariant *
handle_get_property( GDBusConnection  *connection,
                     const gchar      *sender,
                     const gchar      *object_path,
                     const gchar      *interface_name,
                     const gchar      *property_name,
                     GError          **error,
                     gpointer          user_data )
{
  RclHostnameDaemon        *daemon = RCL_HOSTNAME_DAEMON( user_data );
  RclHostnameDaemonPrivate *priv   = daemon->priv;

#define RETURN_STR(field) \
  return g_variant_new_string( priv->field ? priv->field : "" )

  if( g_strcmp0(property_name, "Hostname"                  ) == 0 ) RETURN_STR(hostname);
  if( g_strcmp0(property_name, "StaticHostname"            ) == 0 ) RETURN_STR(static_hostname);
  if( g_strcmp0(property_name, "PrettyHostname"            ) == 0 ) RETURN_STR(pretty_hostname);
  if( g_strcmp0(property_name, "DefaultHostname"           ) == 0 ) RETURN_STR(default_hostname);
  if( g_strcmp0(property_name, "HostnameSource"            ) == 0 ) RETURN_STR(hostname_source);
  if( g_strcmp0(property_name, "IconName"                  ) == 0 ) RETURN_STR(icon_name);
  if( g_strcmp0(property_name, "Chassis"                   ) == 0 ) RETURN_STR(chassis);
  if( g_strcmp0(property_name, "Deployment"                ) == 0 ) RETURN_STR(deployment);
  if( g_strcmp0(property_name, "Location"                  ) == 0 ) RETURN_STR(location);
  if( g_strcmp0(property_name, "KernelName"                ) == 0 ) RETURN_STR(kernel_name);
  if( g_strcmp0(property_name, "KernelRelease"             ) == 0 ) RETURN_STR(kernel_release);
  if( g_strcmp0(property_name, "KernelVersion"             ) == 0 ) RETURN_STR(kernel_version);
  if( g_strcmp0(property_name, "OperatingSystemPrettyName" ) == 0 ) RETURN_STR(os_pretty_name);
  if( g_strcmp0(property_name, "OperatingSystemCPEName"    ) == 0 ) RETURN_STR(os_cpe_name);
  if( g_strcmp0(property_name, "OperatingSystemHomeURL"    ) == 0 ) RETURN_STR(os_home_url);
  if( g_strcmp0(property_name, "HardwareVendor"            ) == 0 ) RETURN_STR(hw_vendor);
  if( g_strcmp0(property_name, "HardwareModel"             ) == 0 ) RETURN_STR(hw_model);
  if( g_strcmp0(property_name, "FirmwareVersion"           ) == 0 ) RETURN_STR(firmware_version);
  if( g_strcmp0(property_name, "FirmwareDate"              ) == 0 ) RETURN_STR(firmware_date);

#undef RETURN_STR

  g_set_error( error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
               "Unknown property '%s'", property_name );
  return NULL;
}

/* Properties are read-only via D-Bus; all changes go through Set* methods. */
static gboolean
handle_set_property( GDBusConnection  *connection,
                     const gchar      *sender,
                     const gchar      *object_path,
                     const gchar      *interface_name,
                     const gchar      *property_name,
                     GVariant         *value,
                     GError          **error,
                     gpointer          user_data )
{
  g_set_error( error, G_DBUS_ERROR, G_DBUS_ERROR_PROPERTY_READ_ONLY,
               "Property '%s' is read-only; use the Set* methods", property_name );
  return FALSE;
}

/* --------------------------------------------------------------------------
   D-Bus method handler
   -------------------------------------------------------------------------- */
static void
handle_method_call( GDBusConnection       *connection,
                    const gchar           *sender,
                    const gchar           *object_path,
                    const gchar           *interface_name,
                    const gchar           *method_name,
                    GVariant              *parameters,
                    GDBusMethodInvocation *invocation,
                    gpointer               user_data )
{
  RclHostnameDaemon        *daemon = RCL_HOSTNAME_DAEMON( user_data );
  RclHostnameDaemonPrivate *priv   = daemon->priv;
  GError *error = NULL;

  /* ------------------------------------------------------------------
     SetHostname – sets transient hostname via sethostname(2)
     ------------------------------------------------------------------ */
  if( g_strcmp0(method_name, "SetHostname") == 0 )
  {
    const gchar *name;
    gboolean     interactive;
    g_variant_get( parameters, "(&sb)", &name, &interactive );

    if( !check_polkit(invocation, RCL_HOSTNAME_POLKIT_SET_HOSTNAME,
                      interactive, &error) )
      goto method_error;

    if( name && name[0] != '\0' )
    {
      if( sethostname(name, strlen(name)) != 0 )
      {
        g_set_error( &error, G_IO_ERROR, g_io_error_from_errno(errno),
                     "sethostname failed: %s", g_strerror(errno) );
        goto method_error;
      }
    }

    rcl_daemon_sync_dbus_properties( daemon );
    g_dbus_method_invocation_return_value( invocation, NULL );
    return;
  }

  /* ------------------------------------------------------------------
     SetStaticHostname – writes /etc/hostname
     ------------------------------------------------------------------ */
  if( g_strcmp0(method_name, "SetStaticHostname") == 0 )
  {
    const gchar *name;
    gboolean     interactive;
    g_variant_get( parameters, "(&sb)", &name, &interactive );

    if( !check_polkit(invocation, RCL_HOSTNAME_POLKIT_SET_STATIC,
                      interactive, &error) )
      goto method_error;

    if( name && name[0] != '\0' )
    {
      gchar *content = g_strdup_printf( "%s\n", name );
      if( !g_file_set_contents(RCL_HOSTNAME_FILE, content, -1, &error) )
      {
        g_free( content );
        goto method_error;
      }
      g_free( content );

      /* Also push to transient hostname so they stay in sync */
      sethostname( name, strlen(name) );
    }
    else
    {
      /* Empty string – remove static hostname file */
      g_unlink( RCL_HOSTNAME_FILE );
    }

    rcl_daemon_sync_dbus_properties( daemon );
    g_dbus_method_invocation_return_value( invocation, NULL );
    return;
  }

  /* ------------------------------------------------------------------
     SetPrettyHostname / SetIconName / SetChassis / SetDeployment / SetLocation
     – all write a key to /etc/machine-info
     ------------------------------------------------------------------ */
  {
    const gchar *key = NULL;
    const gchar *name;
    gboolean     interactive;

    if     ( g_strcmp0(method_name, "SetPrettyHostname") == 0 ) key = "PRETTY_HOSTNAME";
    else if( g_strcmp0(method_name, "SetIconName")       == 0 ) key = "ICON_NAME";
    else if( g_strcmp0(method_name, "SetChassis")        == 0 ) key = "CHASSIS";
    else if( g_strcmp0(method_name, "SetDeployment")     == 0 ) key = "DEPLOYMENT";
    else if( g_strcmp0(method_name, "SetLocation")       == 0 ) key = "LOCATION";

    if( key )
    {
      g_variant_get( parameters, "(&sb)", &name, &interactive );

      if( !check_polkit(invocation, RCL_HOSTNAME_POLKIT_SET_MACHINE_INFO,
                        interactive, &error) )
        goto method_error;

      if( !write_machine_info_key(key, name, &error) )
        goto method_error;

      rcl_daemon_sync_dbus_properties( daemon );
      g_dbus_method_invocation_return_value( invocation, NULL );
      return;
    }
  }

  /* ------------------------------------------------------------------
     GetProductUUID – returns bytes from /sys/class/dmi/id/product_uuid
     ------------------------------------------------------------------ */
  if( g_strcmp0(method_name, "GetProductUUID") == 0 )
  {
    gboolean interactive;
    g_variant_get( parameters, "(b)", &interactive );

    /* Reading product_uuid requires root on most systems; use polkit */
    if( !check_polkit(invocation, RCL_HOSTNAME_POLKIT_SET_HOSTNAME /* reuse */,
                      interactive, &error) )
      goto method_error;

    gchar *uuid_str = read_dmi( RCL_DMI_PRODUCT_UUID );
    GVariantBuilder bytes;
    g_variant_builder_init( &bytes, G_VARIANT_TYPE("ay") );
    for( gsize i = 0; uuid_str[i]; i++ )
      g_variant_builder_add( &bytes, "y", (guchar)uuid_str[i] );
    g_free( uuid_str );

    g_dbus_method_invocation_return_value( invocation,
      g_variant_new("(ay)", &bytes) );
    return;
  }

  /* ------------------------------------------------------------------
     GetHardwareSerial
     ------------------------------------------------------------------ */
  if( g_strcmp0(method_name, "GetHardwareSerial") == 0 )
  {
    gboolean interactive;
    g_variant_get( parameters, "(b)", &interactive );

    if( !check_polkit(invocation, RCL_HOSTNAME_POLKIT_SET_HOSTNAME /* reuse */,
                      interactive, &error) )
      goto method_error;

    gchar *serial = read_dmi( RCL_DMI_CHASSIS_SERIAL );
    g_dbus_method_invocation_return_value( invocation,
      g_variant_new("(s)", serial) );
    g_free( serial );
    return;
  }

  /* ------------------------------------------------------------------
     Describe – return all properties as a JSON blob
     ------------------------------------------------------------------ */
  if( g_strcmp0(method_name, "Describe") == 0 )
  {
    gchar *json = g_strdup_printf(
      "{"
      "\"Hostname\":\"%s\","
      "\"StaticHostname\":\"%s\","
      "\"PrettyHostname\":\"%s\","
      "\"IconName\":\"%s\","
      "\"Chassis\":\"%s\","
      "\"Deployment\":\"%s\","
      "\"Location\":\"%s\","
      "\"KernelName\":\"%s\","
      "\"KernelRelease\":\"%s\","
      "\"KernelVersion\":\"%s\","
      "\"OperatingSystemPrettyName\":\"%s\","
      "\"HardwareVendor\":\"%s\","
      "\"HardwareModel\":\"%s\""
      "}",
      priv->hostname         ? priv->hostname         : "",
      priv->static_hostname  ? priv->static_hostname  : "",
      priv->pretty_hostname  ? priv->pretty_hostname  : "",
      priv->icon_name        ? priv->icon_name        : "",
      priv->chassis          ? priv->chassis          : "",
      priv->deployment       ? priv->deployment       : "",
      priv->location         ? priv->location         : "",
      priv->kernel_name      ? priv->kernel_name      : "",
      priv->kernel_release   ? priv->kernel_release   : "",
      priv->kernel_version   ? priv->kernel_version   : "",
      priv->os_pretty_name   ? priv->os_pretty_name   : "",
      priv->hw_vendor        ? priv->hw_vendor        : "",
      priv->hw_model         ? priv->hw_model         : "" );

    g_dbus_method_invocation_return_value( invocation,
      g_variant_new("(s)", json) );
    g_free( json );
    return;
  }

  /* Unknown method */
  g_dbus_method_invocation_return_error( invocation,
    G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
    "Unknown method '%s'", method_name );
  return;

method_error:
  g_dbus_method_invocation_return_gerror( invocation, error );
  g_error_free( error );
}

/* --------------------------------------------------------------------------
   D-Bus vtable
   -------------------------------------------------------------------------- */
static const GDBusInterfaceVTable interface_vtable = {
  handle_method_call,
  handle_get_property,
  handle_set_property,
};

/* --------------------------------------------------------------------------
   RclDaemon virtual method implementations
   -------------------------------------------------------------------------- */

RclDaemon *
rcl_daemon_new( void )
{
  return RCL_DAEMON( g_object_new(RCL_TYPE_HOSTNAME_DAEMON, NULL) );
}

gboolean
rcl_daemon_startup( RclDaemon *daemon, GDBusConnection *connection )
{
  RclHostnameDaemon        *self = RCL_HOSTNAME_DAEMON( daemon );
  RclHostnameDaemonPrivate *priv = self->priv;
  GDBusInterfaceInfo       *iface_info;
  GError *error = NULL;

  priv->connection    = g_object_ref( connection );
  priv->introspection = g_dbus_node_info_new_for_xml(
                          hostname1_introspection_xml, &error );
  if( !priv->introspection )
  {
    g_warning( "Failed to parse introspection XML: %s", error->message );
    g_error_free( error );
    return FALSE;
  }

  iface_info = g_dbus_node_info_lookup_interface(
                 priv->introspection, "org.freedesktop.hostname1" );

  priv->registration_id = g_dbus_connection_register_object(
    connection,
    "/org/freedesktop/hostname1",
    iface_info,
    &interface_vtable,
    self,
    NULL,
    &error );

  if( priv->registration_id == 0 )
  {
    g_warning( "Could not register D-Bus object: %s", error->message );
    g_error_free( error );
    return FALSE;
  }

  /* Populate initial property values */
  rcl_daemon_sync_dbus_properties( self );

  g_debug( "hostnamed D-Bus object registered at /org/freedesktop/hostname1" );
  return TRUE;
}

void
rcl_daemon_shutdown( RclDaemon *daemon )
{
  RclHostnameDaemon        *self = RCL_HOSTNAME_DAEMON( daemon );
  RclHostnameDaemonPrivate *priv = self->priv;

  if( priv->connection && priv->registration_id > 0 )
  {
    g_dbus_connection_unregister_object( priv->connection,
                                         priv->registration_id );
    priv->registration_id = 0;
  }

  g_clear_object( &priv->connection );

  if( priv->introspection )
  {
    g_dbus_node_info_unref( priv->introspection );
    priv->introspection = NULL;
  }
}

void
rcl_daemon_set_debug( RclDaemon *daemon, gboolean debug )
{
  RCL_HOSTNAME_DAEMON(daemon)->priv->debug = debug;
}

/* --------------------------------------------------------------------------
   GObject lifecycle
   -------------------------------------------------------------------------- */

static void
rcl_hostname_daemon_finalize( GObject *object )
{
  RclHostnameDaemonPrivate *priv = RCL_HOSTNAME_DAEMON(object)->priv;

  g_free( priv->hostname );
  g_free( priv->static_hostname );
  g_free( priv->pretty_hostname );
  g_free( priv->default_hostname );
  g_free( priv->hostname_source );
  g_free( priv->icon_name );
  g_free( priv->chassis );
  g_free( priv->deployment );
  g_free( priv->location );
  g_free( priv->kernel_name );
  g_free( priv->kernel_release );
  g_free( priv->kernel_version );
  g_free( priv->os_pretty_name );
  g_free( priv->os_cpe_name );
  g_free( priv->os_home_url );
  g_free( priv->hw_vendor );
  g_free( priv->hw_model );
  g_free( priv->firmware_version );
  g_free( priv->firmware_date );

  G_OBJECT_CLASS(rcl_hostname_daemon_parent_class)->finalize(object);
}

static void
rcl_hostname_daemon_class_init( RclHostnameDaemonClass *klass )
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  object_class->finalize = rcl_hostname_daemon_finalize;
}

static void
rcl_hostname_daemon_init( RclHostnameDaemon *self )
{
  self->priv = rcl_hostname_daemon_get_instance_private( self );
}

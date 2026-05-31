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

#pragma once

#include <gio/gio.h>
#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

/*---------------------------------------------------------------------------
  RclDaemon – abstract base type shared with main.c
  (Mirrors the pattern used in rcl-timedate.h so main.c stays consistent.)
 ---------------------------------------------------------------------------*/

#define RCL_TYPE_DAEMON       (rcl_daemon_get_type())
#define RCL_DAEMON(o)         (G_TYPE_CHECK_INSTANCE_CAST ((o), RCL_TYPE_DAEMON, RclDaemon))
#define RCL_IS_DAEMON(o)      (G_TYPE_CHECK_INSTANCE_TYPE ((o), RCL_TYPE_DAEMON))

typedef struct _RclDaemon      RclDaemon;
typedef struct _RclDaemonClass RclDaemonClass;

struct _RclDaemon
{
  GObject parent_instance;
};

struct _RclDaemonClass
{
  GObjectClass parent_class;
};

GType      rcl_daemon_get_type   ( void ) G_GNUC_CONST;
RclDaemon *rcl_daemon_new        ( void );
gboolean   rcl_daemon_startup    ( RclDaemon *daemon, GDBusConnection *connection );
void       rcl_daemon_shutdown   ( RclDaemon *daemon );
void       rcl_daemon_set_debug  ( RclDaemon *daemon, gboolean debug );

/*---------------------------------------------------------------------------
  RclHostnameDaemon – the concrete GObject that owns the D-Bus skeleton for
  org.freedesktop.hostname1.

  Properties exposed on the bus (all read-only for clients; written via the
  Set* methods with polkit authorisation):

    Hostname          – transient hostname (sethostname(2))
    StaticHostname    – value stored in /etc/HOSTNAME
    PrettyHostname    – human-readable name from /etc/machine-info
    DefaultHostname   – compiled-in or distro default (read-only)
    HostnameSource    – one of: "static", "transient", "default"  (string)
    IconName          – e.g. "computer-laptop" from /etc/machine-info or chassis
    Chassis           – "desktop", "laptop", "server", "tablet", "handset", …
    Deployment        – e.g. "production" from /etc/machine-info
    Location          – freeform location string from /etc/machine-info
    KernelName        – uname.sysname  (e.g. "Linux")
    KernelRelease     – uname.release  (e.g. "6.6.0")
    KernelVersion     – uname.version
    OperatingSystemPrettyName   – from /etc/os-release PRETTY_NAME
    OperatingSystemCPEName      – from /etc/os-release CPE_NAME (may be empty)
    OperatingSystemHomeURL      – from /etc/os-release HOME_URL  (may be empty)
    HardwareVendor    – from DMI or device-tree (may be empty)
    HardwareModel     – from DMI or device-tree (may be empty)
    FirmwareVersion   – from DMI (may be empty)
    FirmwareDate      – from DMI (may be empty)

  Methods:
    SetHostname       (s name, b interactive)  – sets transient hostname
    SetStaticHostname (s name, b interactive)  – writes /etc/HOSTNAME
    SetPrettyHostname (s name, b interactive)  – writes PRETTY_HOSTNAME to /etc/machine-info
    SetIconName       (s name, b interactive)  – writes ICON_NAME to /etc/machine-info
    SetChassis        (s name, b interactive)  – writes CHASSIS to /etc/machine-info
    SetDeployment     (s name, b interactive)  – writes DEPLOYMENT to /etc/machine-info
    SetLocation       (s name, b interactive)  – writes LOCATION to /etc/machine-info
    GetProductUUID    (b interactive) → ay      – returns DMI product UUID as byte array
    GetHardwareSerial (b interactive) → s       – returns DMI chassis serial number
    Describe          ()             → s        – returns JSON blob of all properties

  The 'interactive' boolean follows the systemd convention: when TRUE the
  caller is willing to wait for a polkit authentication dialog; when FALSE
  the call should fail immediately if the caller is not already authorised.
 ---------------------------------------------------------------------------*/

#define RCL_TYPE_HOSTNAME_DAEMON        (rcl_hostname_daemon_get_type())
#define RCL_HOSTNAME_DAEMON(o)          (G_TYPE_CHECK_INSTANCE_CAST  ((o), RCL_TYPE_HOSTNAME_DAEMON, RclHostnameDaemon))
#define RCL_HOSTNAME_DAEMON_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST     ((k), RCL_TYPE_HOSTNAME_DAEMON, RclHostnameDaemonClass))
#define RCL_IS_HOSTNAME_DAEMON(o)       (G_TYPE_CHECK_INSTANCE_TYPE  ((o), RCL_TYPE_HOSTNAME_DAEMON))
#define RCL_IS_HOSTNAME_DAEMON_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE     ((k), RCL_TYPE_HOSTNAME_DAEMON))
#define RCL_HOSTNAME_DAEMON_GET_CLASS(o)(G_TYPE_INSTANCE_GET_CLASS   ((o), RCL_TYPE_HOSTNAME_DAEMON, RclHostnameDaemonClass))

typedef struct _RclHostnameDaemon      RclHostnameDaemon;
typedef struct _RclHostnameDaemonClass RclHostnameDaemonClass;
typedef struct _RclHostnameDaemonPrivate RclHostnameDaemonPrivate;

struct _RclHostnameDaemon
{
  RclDaemon                 parent_instance;
  RclHostnameDaemonPrivate *priv;
};

struct _RclHostnameDaemonClass
{
  RclDaemonClass parent_class;
};

GType              rcl_hostname_daemon_get_type ( void ) G_GNUC_CONST;

/*
 * rcl_daemon_sync_dbus_properties:
 *
 * Re-reads /etc/hostname, /etc/machine-info, /etc/os-release, uname(2),
 * and any DMI sysfs nodes, then emits PropertiesChanged on the D-Bus
 * object for every value that has changed since the last call.
 *
 * Called from main.c's inotify callback whenever a watched file changes.
 * Also called once during startup (from rcl_daemon_startup) to populate
 * the initial property values.
 */
void rcl_daemon_sync_dbus_properties ( RclHostnameDaemon *daemon );

/*---------------------------------------------------------------------------
  Polkit action IDs used when authorising Set* method calls.
  Defined here so both the daemon implementation and any test harness can
  reference the same strings.
 ---------------------------------------------------------------------------*/

#define RCL_HOSTNAME_POLKIT_SET_HOSTNAME   "org.freedesktop.hostname1.set-hostname"
#define RCL_HOSTNAME_POLKIT_SET_STATIC     "org.freedesktop.hostname1.set-static-hostname"
#define RCL_HOSTNAME_POLKIT_SET_MACHINE_INFO \
                                           "org.freedesktop.hostname1.set-machine-info"

/*---------------------------------------------------------------------------
  File paths – gathered in one place so they're easy to override for tests
  or alternative distro layouts.
 ---------------------------------------------------------------------------*/

#define RCL_HOSTNAME_FILE          "/etc/HOSTNAME"
#define RCL_MACHINE_INFO_FILE      "/etc/machine-info"
#define RCL_OS_RELEASE_FILE        "/etc/os-release"

/* DMI sysfs paths (Linux-specific) */
#define RCL_DMI_CHASSIS_TYPE       "/sys/class/dmi/id/chassis_type"
#define RCL_DMI_CHASSIS_VENDOR     "/sys/class/dmi/id/chassis_vendor"
#define RCL_DMI_PRODUCT_NAME       "/sys/class/dmi/id/product_name"
#define RCL_DMI_PRODUCT_UUID       "/sys/class/dmi/id/product_uuid"
#define RCL_DMI_CHASSIS_SERIAL     "/sys/class/dmi/id/chassis_serial"
#define RCL_DMI_BIOS_VERSION       "/sys/class/dmi/id/bios_version"
#define RCL_DMI_BIOS_DATE          "/sys/class/dmi/id/bios_date"
#define RCL_DMI_BOARD_VENDOR       "/sys/class/dmi/id/board_vendor"

/*---------------------------------------------------------------------------
  Chassis type helpers.

  chassis_type_to_icon_name() maps the numeric SMBIOS chassis type (read
  from RCL_DMI_CHASSIS_TYPE) to a freedesktop icon name string.

  chassis_type_to_string() maps it to the human-readable chassis string
  used for the Chassis D-Bus property.
 ---------------------------------------------------------------------------*/

const gchar *rcl_chassis_type_to_icon_name ( guint chassis_type );
const gchar *rcl_chassis_type_to_string    ( guint chassis_type );

G_END_DECLS

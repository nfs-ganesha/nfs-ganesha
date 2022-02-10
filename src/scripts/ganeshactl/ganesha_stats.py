#!/usr/bin/python3
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# Copyright (C) 2014 IBM
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# Author: Marc Eshel <eshel@us.ibm.com>
#
# This command receives statistics from Ganesha over DBus. The format
# for a command is:
#
# ganesha_stats <subcommand> <args>
#
# ganesha_stats help
#       To get detaled help
#
from __future__ import print_function
import sys
import Ganesha.glib_dbus_stats

def print_usage_exit(return_code):
    message = (
"""

Usage:

Command displays global stats by default.

To display current status regarding stat counting use:
  {progname} status

To display stat counters use:
  {progname} [list_clients | deleg <ip address>
              inode | iov3 [export id] | iov4 [export id] | iov41 [export id] | iov42 [export id] |
              iomon [export id] | export | total [export id] | fast | pnfs [export id] |
              fsal <fsal name> | v3_full | v4_full | auth |
              client_io_ops <ip address> | export_details <export id> |
              client_all_ops <ip address>]

To display stat counters in json format use:
  {progname} json <command>

To reset stat counters use:
  {progname} reset

To enable/disable stat counters use:
  {progname} [enable | disable] [all | nfs | fsal | v3_full |
                                 v4_full | auth | client_all_ops]

"""
    )
    print(message.format(progname=sys.argv[0]))
    sys.exit(return_code)

output_json = False
if (len(sys.argv) < 2):
    command = 'global'
else:
    command = sys.argv[1]
    opts = sys.argv[2:]

if command == 'json':
    output_json = True
    if len(opts) >= 1:
        command = opts[0]
        opts = opts[1:]
    else:
        command = DEFAULT_COMMAND

json_not_available = ('fsal', 'reset', 'enable')
if output_json and command in json_not_available:
    sys.exit("{0} command is not available in json.".format(command))

# check arguments
commands = (
    'help', 'list_clients', 'deleg', 'global', 'inode', 'iov3', 'iov4',
    'iov41', 'iov42', 'iomon', 'export', 'total', 'fast', 'pnfs', 'fsal',
    'reset', 'enable', 'disable', 'status', 'v3_full', 'v4_full', 'auth',
    'client_io_ops', 'export_details', 'client_all_ops', 'json'
)

if command not in commands:
    print("\nError: Option '%s' is not correct." % command)
    print_usage_exit(1)
# requires an IP address
elif command in ('deleg', 'client_io_ops', 'client_all_ops'):
    if not len(opts) == 1:
        print("\nError: Option '%s' must be followed by an ip address." % command)
        print_usage_exit(1)
    command_arg = opts[0]
# requires an export id
elif command == 'export_details':
    if not len(opts) == 1:
        print("\nError: Option '%s' must be followed by an export id." % command)
        print_usage_exit(1)
    if opts[0].isdigit():
        command_arg = int(opts[0])
    else:
        print("\nError: Argument '%s' must be numeric." % opts[0])
        print_usage_exit(1)
# optionally accepts an export id
elif command in ('iov3', 'iov4', 'iov41', 'iov42', 'iomon', 'total', 'pnfs'):
    if (len(opts) == 0):
        command_arg = -1
    elif (len(opts) == 1) and opts[0].isdigit():
        command_arg = int(opts[0])
    else:
        print_usage_exit(1)
# requires fsal name
elif command in ('fsal'):
    if not len(opts) == 1:
        print("\nError: Option '%s' must be followed by fsal name." % command)
        print_usage_exit(1)
    command_arg = opts[0]
elif command in ('enable', 'disable'):
    if not len(opts) == 1:
        print("\nError: Option '%s' must be followed by all/nfs/fsal/v3_full/v4_full/auth/client_all_ops" %
            command)
        print_usage_exit(1)
    command_arg = opts[0]
    if command_arg not in ('all', 'nfs', 'fsal', 'v3_full', 'v4_full', 'auth', 'client_all_ops'):
        print("\nError: Option '%s' must be followed by all/nfs/fsal/v3_full/v4_full/auth/client_all_ops" %
            command)
        print_usage_exit(1)
elif command == "help":
    print_usage_exit(0)

# retrieve and print stats
try:
    exp_interface = Ganesha.glib_dbus_stats.RetrieveExportStats()
    cl_interface = Ganesha.glib_dbus_stats.RetrieveClientStats()

    result = None
    if command == "global":
        result = exp_interface.global_stats()
    elif command == "export":
        result = exp_interface.export_stats()
    elif command == "inode":
        result = exp_interface.inode_stats()
    elif command == "fast":
        result = exp_interface.fast_stats()
    elif command == "list_clients":
        result = cl_interface.list_clients()
    elif command == "deleg":
        result = cl_interface.deleg_stats(command_arg)
    elif command == "client_io_ops":
        result = cl_interface.client_io_ops_stats(command_arg)
    elif command == "client_all_ops":
        result = cl_interface.client_all_ops_stats(command_arg)
    elif command == "iov3":
        result = exp_interface.v3io_stats(command_arg)
    elif command == "iov4":
        result = exp_interface.v4io_stats(command_arg)
    elif command == "iov41":
        result = exp_interface.v41io_stats(command_arg)
    elif command == "iov42":
        result = exp_interface.v42io_stats(command_arg)
    elif command == "iomon":
        result = exp_interface.iomon_stats(command_arg)
    elif command == "total":
        result = exp_interface.total_stats(command_arg)
    elif command == "export_details":
        result = exp_interface.export_details_stats(command_arg)
    elif command == "pnfs":
        result = exp_interface.pnfs_stats(command_arg)
    elif command == "reset":
        result = exp_interface.reset_stats()
    elif command == "fsal":
        result = exp_interface.fsal_stats(command_arg)
    elif command == "v3_full":
        result = exp_interface.v3_full_stats()
    elif command == "v4_full":
        result = exp_interface.v4_full_stats()
    elif command == "auth":
        result = exp_interface.auth_stats()
    elif command == "enable":
        result = exp_interface.enable_stats(command_arg)
    elif command == "disable":
        result = exp_interface.disable_stats(command_arg)
    elif command == "status":
        result = exp_interface.status_stats()

    print(result.json()) if output_json else print(result)
except:
    sys.exit("Error: Can't talk to ganesha service on d-bus. Looks like Ganesha is down")

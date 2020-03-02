#!/usr/bin/python3
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
import dbus

def print_usage_exit(return_code):
    message = "\nUsage: \n"
    message += "Command displays global stats by default.\n"
    message += "\nTo display current status regarding stat counting use: \n"
    message += "  %s status \n" % (sys.argv[0])
    message += "\nTo display stat counters use: \n"
    message += "  %s [ list_clients | deleg <ip address> |\n" % (sys.argv[0])
    message += "          inode | iov3 [export id] | iov4 [export id] |\n"
    message += "          export | total [export id] | fast | pnfs [export id] |\n"
    message += "          fsal <fsal name> | v3_full | v4_full | auth |\n"
    message += "          client_io_ops <ip address> | export_details <export id> |\n"
    message += "          client_all_ops <ip address>] \n"
    message += "\nTo reset stat counters use: \n"
    message += "  %s reset \n" % (sys.argv[0])
    message += "\nTo enable/disable stat counters use: \n"
    message += "  %s [ enable | disable] [all | nfs | fsal | v3_full |\n" % (sys.argv[0])
    message += "           v4_full | auth | client_all_ops] \n"
    print(message)
    sys.exit(return_code)

if (len(sys.argv) < 2):
    command = 'global'
else:
    command = sys.argv[1]

# check arguments
commands = ('help', 'list_clients', 'deleg', 'global', 'inode', 'iov3',
            'iov4', 'export', 'total', 'fast', 'pnfs', 'fsal', 'reset', 'enable',
            'disable', 'status', 'v3_full', 'v4_full', 'auth', 'client_io_ops',
            'export_details', 'client_all_ops')
if command not in commands:
    print("\nError: Option '%s' is not correct." % command)
    print_usage_exit(1)
# requires an IP address
elif command in ('deleg', 'client_io_ops', 'client_all_ops'):
    if not len(sys.argv) == 3:
        print("\nError: Option '%s' must be followed by an ip address." % command)
        print_usage_exit(1)
    command_arg = sys.argv[2]
# requires an export id
elif command == 'export_details':
    if not len(sys.argv) == 3:
        print("\nError: Option '%s' must be followed by an export id." % command)
        print_usage_exit(1)
    if sys.argv[2].isdigit():
        command_arg = int(sys.argv[2])
    else:
        print("\nError: Argument '%s' must be numeric." % sys.argv[2])
        print_usage_exit(1)
# optionally accepts an export id
elif command in ('iov3', 'iov4', 'total', 'pnfs'):
    if (len(sys.argv) == 2):
        command_arg = -1
    elif (len(sys.argv) == 3) and sys.argv[2].isdigit():
        command_arg = int(sys.argv[2])
    else:
        print_usage_exit(1)
# requires fsal name
elif command in ('fsal'):
    if not len(sys.argv) == 3:
        print("\nError: Option '%s' must be followed by fsal name." % command)
        print_usage_exit(1)
    command_arg = sys.argv[2]
elif command in ('enable', 'disable'):
    if not len(sys.argv) == 3:
        print("\nError: Option '%s' must be followed by all/nfs/fsal/v3_full/v4_full/auth/client_all_ops" %
            command)
        print_usage_exit(1)
    command_arg = sys.argv[2]
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
    if command == "global":
        print(exp_interface.global_stats())
    elif command == "export":
        print(exp_interface.export_stats())
    elif command == "inode":
        print(exp_interface.inode_stats())
    elif command == "fast":
        print(exp_interface.fast_stats())
    elif command == "list_clients":
        print(cl_interface.list_clients())
    elif command == "deleg":
        print(cl_interface.deleg_stats(command_arg))
    elif command == "client_io_ops":
        print(cl_interface.client_io_ops_stats(command_arg))
    elif command == "client_all_ops":
        print(cl_interface.client_all_ops_stats(command_arg))
    elif command == "iov3":
        print(exp_interface.v3io_stats(command_arg))
    elif command == "iov4":
        print(exp_interface.v4io_stats(command_arg))
    elif command == "total":
        print(exp_interface.total_stats(command_arg))
    elif command == "export_details":
        print(exp_interface.export_details_stats(command_arg))
    elif command == "pnfs":
        print(exp_interface.pnfs_stats(command_arg))
    elif command == "reset":
        print(exp_interface.reset_stats())
    elif command == "fsal":
        print(exp_interface.fsal_stats(command_arg))
    elif command == "v3_full":
        print(exp_interface.v3_full_stats())
    elif command == "v4_full":
        print(exp_interface.v4_full_stats())
    elif command == "auth":
        print(exp_interface.auth_stats())
    elif command == "enable":
        print(exp_interface.enable_stats(command_arg))
    elif command == "disable":
        print(exp_interface.disable_stats(command_arg))
    elif command == "status":
        print(exp_interface.status_stats())
except dbus.exceptions.DBusException:
    sys.exit("Error: Can't talk to ganesha service on d-bus. Looks like Ganesha is down")

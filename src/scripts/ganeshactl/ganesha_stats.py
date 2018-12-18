#!/usr/bin/python2
#
# This command receives statistics from Ganesha over DBus. The format for a command is:
# ./ganesha_stats.py <command> <option>
# eg. ./ganesha_stats.py deleg ::ffff:192.168.122.94
# To get a list of clients and client ips use the "list_clients " command.
#
from __future__ import print_function
import gobject
import sys
import time
import re
import Ganesha.glib_dbus_stats

def usage():
    message = "Command displays global stats by default.\n"
    message += "To display current status regarding stat counting use \n"
    message += "%s status \n" % (sys.argv[0])
    message += "To display stat counters use \n"
    message += "%s [list_clients | deleg <ip address> | " % (sys.argv[0])
    message += "inode | iov3 [export id] | iov4 [export id] | export |"
    message += " total [export id] | fast | pnfs [export id] |"
    message += " fsal <fsal name> ] \n"
    message += "To reset stat counters use \n"
    message += "%s reset \n" % (sys.argv[0])
    message += "To enable/disable stat counters use \n"
    message += "%s [enable | disable] [all | nfs | fsal] " % (sys.argv[0])
    sys.exit(message)

if len(sys.argv) < 2:
    command = 'global'
else:
    command = sys.argv[1]

# check arguments
commands = ('help', 'list_clients', 'deleg', 'global', 'inode', 'iov3', 'iov4',
	    'export', 'total', 'fast', 'pnfs', 'fsal', 'reset', 'enable',
	    'disable', 'pool')
if command not in commands:
    print("Option \"%s\" is not correct." % (command))
    usage()
# requires an IP address
elif command in ('deleg'):
    if not len(sys.argv) == 3:
        print("Option \"%s\" must be followed by an ip address." % (command))
        usage()
    command_arg = sys.argv[2]
# optionally accepts an export id
elif command in ('iov3', 'iov4', 'total', 'pnfs'):
    if (len(sys.argv) == 2):
        command_arg = -1
    elif (len(sys.argv) == 3) and sys.argv[2].isdigit():
        command_arg = sys.argv[2]
    else:
        usage()
elif command == "help":
    usage()
# requires fsal name
elif command in ('fsal'):
    if not len(sys.argv) == 3:
        print("Option \"%s\" must be followed by fsal name." % (command))
        usage()
    command_arg = sys.argv[2]
elif command in ('enable', 'disable'):
    if not len(sys.argv) == 3:
        print("Option \"%s\" must be followed by all/nfs/fsal." % (command))
        usage()
    command_arg = sys.argv[2]
    if command_arg not in ('all', 'nfs', 'fsal'):
        print("Option \"%s\" must be followed by all/nfs/fsal." % (command))
        usage()

# retrieve and print(stats
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
elif command == "iov3":
    print(exp_interface.v3io_stats(command_arg))
elif command == "iov4":
    print(exp_interface.v4io_stats(command_arg))
elif command == "total":
    print(exp_interface.total_stats(command_arg))
elif command == "pnfs":
    print(exp_interface.pnfs_stats(command_arg))
elif command == "reset":
    print(exp_interface.reset_stats())
elif command == "fsal":
    print(exp_interface.fsal_stats(command_arg))
elif command == "enable":
    print(exp_interface.enable_stats(command_arg))
elif command == "disable":
    print(exp_interface.disable_stats(command_arg))
elif command == "status":
    print(exp_interface.status_stats())

#!/usr/bin/python
#
# This command receives statistics from Ganesha over DBus. The format for a command is:
# ./ganesha_stats.py <command> <option>
# eg. ./ganesha_stats.py deleg ::ffff:192.168.122.94
# To get a list of clients and client ips use the "list_clients " command.
#

import gobject
import sys
import time
import re
import Ganesha.glib_dbus_stats

def usage():
    message = "Command gives global stats by default.\n"
    message += "%s [list_clients | deleg <ip address> | " % (sys.argv[0])
    message += "inode | iov3 [export id] | iov4 [export id] | export |"
    message += " total [export id] | fast | pnfs [export id] ]"
    sys.exit(message)

if len(sys.argv) < 2:
    command = 'global'
else:
    command = sys.argv[1]

# check arguments
commands = ('help', 'list_clients', 'deleg', 'global', 'inode', 'iov3', 'iov4',
           'export', 'total', 'fast', 'pnfs')
if command not in commands:
    print "Option \"%s\" is not correct." % (command)
    usage()
# requires an IP address
elif command in ('deleg'):
    if not len(sys.argv) == 3:
        print "Option \"%s\" must be followed by an ip address." % (command)
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

# retrieve and print stats
exp_interface = Ganesha.glib_dbus_stats.RetrieveExportStats()
cl_interface = Ganesha.glib_dbus_stats.RetrieveClientStats()
if command == "global":
    print exp_interface.global_stats()
elif command == "export":
    print exp_interface.export_stats()
elif command == "inode":
    print exp_interface.inode_stats()
elif command == "fast":
    print exp_interface.fast_stats()
elif command == "list_clients":
    print cl_interface.list_clients()
elif command == "deleg":
    print cl_interface.deleg_stats(command_arg)
elif command == "iov3":
    print exp_interface.v3io_stats(command_arg)
elif command == "iov4":
    print exp_interface.v4io_stats(command_arg)
elif command == "total":
    print exp_interface.total_stats(command_arg)
elif command == "pnfs":
    print exp_interface.pnfs_stats(command_arg)

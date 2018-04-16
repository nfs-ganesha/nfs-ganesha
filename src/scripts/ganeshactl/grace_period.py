#!/usr/bin/python2

# You must initialize the gobject/dbus support for threading
# before doing anything.
from __future__ import print_function
import gobject
import sys

ipaddr=sys.argv[1]
print('event:ip_addr=', ipaddr)

gobject.threads_init()

from dbus import glib
glib.init_threads()

# Create a session bus.
import dbus
bus = dbus.SystemBus()

# Create an object that will proxy for a particular remote object.
try:
	admin = bus.get_object("org.ganesha.nfsd",
        	               "/org/ganesha/nfsd/admin")
except dbus.exceptions.DBusException as e:
	sys.exit("Error: Can't talk to ganesha service on d-bus." \
		 " Looks like Ganesha is down")

# call method
ganesha_grace = admin.get_dbus_method('grace',
                               'org.ganesha.nfsd.admin')

print("Start grace period.")

try:
	print(ganesha_grace(ipaddr))
except dbus.exceptions.DBusException as e:
	sys.exit("Error: Failed to start grace period")

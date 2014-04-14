#!/usr/bin/python

# You must initialize the gobject/dbus support for threading
# before doing anything.
import gobject
import sys

gobject.threads_init()

from dbus import glib
glib.init_threads()

# Create a session bus.
import dbus
bus = dbus.SystemBus()

# Create an object that will proxy for a particular remote object.
try:
	admin = bus.get_object("org.ganesha.nfsd",
                       "/org/ganesha/nfsd/ExportMgr")
except: # catch *all* exceptions
      print "Error: Can't talk to ganesha service on d-bus. Looks like Ganesha is down"
      exit(1) 

# call method
ganesha_nfsstats_ops = admin.get_dbus_method('GetFastOPS',
                               'org.ganesha.nfsd.exportstats')

total_ops=ganesha_nfsstats_ops(0)
if total_ops[1] != "OK":
	print "No NFS activity"
else:
	print "Global ops:"
	for stat in total_ops[3]:
		print stat,
	print

exit(0)

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
ganesha_exports = admin.get_dbus_method('ShowExports',
                              	'org.ganesha.nfsd.exportmgr')
ganesha_nfsstats_ops = admin.get_dbus_method('GetTotalOPS',
                               'org.ganesha.nfsd.exportstats')

export_list=ganesha_exports()
for exports in export_list[1]:
	export_id=exports[0]
	total_ops=ganesha_nfsstats_ops(export_id)
	if total_ops[1] == "Export does not have any activity":
		print "No NFS activity"
	else:
		print "Total for:",exports[1]
		for stat in total_ops[3]:
			print '\t', stat,
		print

exit(0)

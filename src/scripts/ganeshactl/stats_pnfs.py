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
ganesha_nfsstats_v4 = admin.get_dbus_method('GetNFSv41Layouts',
                               'org.ganesha.nfsd.exportstats')


export_list=ganesha_exports()
for exports in export_list[1]:
	export_id=exports[0]
	stats_v4=ganesha_nfsstats_v4(export_id)
	if stats_v4[1] == "Export does not have any NFSv4.1 activity":
		print "No pNFS activity for:",exports[1]
	else:
		print "Statistics for:",exports[1],"\n\t\ttotal\terrors\tdelays"
		print 'getdevinfo ',
		for stat in stats_v4[3]:
			print '\t', stat,
		print
		print 'layout_get ',
		for stat in stats_v4[4]:
			print '\t', stat,
		print
		print 'layout_commit ',
		for stat in stats_v4[5]:
			print '\t', stat,
		print
		print 'layout_return ',
		for stat in stats_v4[6]:
			print '\t', stat,
		print
		print 'recall ',
		for stat in stats_v4[7]:
			print '\t', stat,
		print

exit(0)
